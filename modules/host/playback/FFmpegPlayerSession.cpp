#include "FFmpegPlayer.h"

#include "FFmpegPlayerInternal.h"

#include "FFmpegAudioOutput.h"
#include "FFmpegError.h"
#include "FFmpegHwDecode.h"
#include "FFmpegPacketQueue.h"
#include "FFmpegTimeline.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <future>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace ffplay_detail;

// Pin the FFmpeg-free mirrors in FFmpegError.h against the real AVERROR_* values, so a
// future libav change (or a typo in a mirror) is a compile error here rather than a
// silent misclassification at runtime. ENOENT/EACCES use the errno convention AVERROR(e).
static_assert(kAvErrInvalidData == AVERROR_INVALIDDATA, "AVERROR_INVALIDDATA mirror drifted");
static_assert(kAvErrEof == AVERROR_EOF, "AVERROR_EOF mirror drifted");
static_assert(kAvErrDemuxerNotFound == AVERROR_DEMUXER_NOT_FOUND, "AVERROR_DEMUXER_NOT_FOUND mirror drifted");
static_assert(kAvErrDecoderNotFound == AVERROR_DECODER_NOT_FOUND, "AVERROR_DECODER_NOT_FOUND mirror drifted");
static_assert(kAvErrProtocolNotFound == AVERROR_PROTOCOL_NOT_FOUND, "AVERROR_PROTOCOL_NOT_FOUND mirror drifted");
static_assert(kAvErrStreamNotFound == AVERROR_STREAM_NOT_FOUND, "AVERROR_STREAM_NOT_FOUND mirror drifted");
static_assert(kAvErrBsfNotFound == AVERROR_BSF_NOT_FOUND, "AVERROR_BSF_NOT_FOUND mirror drifted");
static_assert(kAvErrNoEnt == AVERROR(ENOENT), "AVERROR(ENOENT) mirror drifted");
static_assert(kAvErrAccess == AVERROR(EACCES), "AVERROR(EACCES) mirror drifted");

// ── Demux session ─────────────────────────────────────────────────────────────

// Everything a demux session holds for one file, passed between the PlayFile
// phases. Owned resources (fmt, vDec, aud, sDec) are freed by CloseSession (or
// by BindSelectedTracks' failure path); hw is declared after vDec in spirit —
// the decoder must be freed *before* hw is destroyed because it unwinds through
// the device ctx / get_format on free, which CloseSession's ordering guarantees.
struct FFmpegPlayer::SessionContext
{
    AVFormatContext* fmt = nullptr;
    const AVCodec* vCodec = nullptr;
    int vIdx = -1;            // video stream index (-1 = none found)
    int defaultAudioIdx = -1; // container's best audio stream index
    AVCodecContext* vDec = nullptr;
    AVStream* vStream = nullptr;
    FFmpegHwDecode hw;
    int W = 0;
    int H = 0;
    AudioBinding aud;
    int subIdx = -1; // embedded subtitle stream routed to subQ_ (-1 = none)
    AVCodecContext* sDec = nullptr;
    AVStream* sStream = nullptr;
    bool isImage = false; // single video frame, no audio (still image / slideshow)
    double durationSec = 0.0;
    double timelineStartSec = 0.0; // absolute FFmpeg timestamp corresponding to player time 0
};

namespace
{
std::optional<double> StreamStartSeconds(const AVStream* stream)
{
    if (!stream || stream->start_time == AV_NOPTS_VALUE)
    {
        return std::nullopt;
    }
    return static_cast<double>(stream->start_time) * av_q2d(stream->time_base);
}
} // namespace

void FFmpegPlayer::PlayFile(const std::string& path, double resumePos)
{
    FRAMELIFT_PERF_START("file-open");
    FRAMELIFT_PERF_START("file-load-metadata");
    QueueEvent(MakeLifecycle(MediaEventType::StartFile));
    SetIdle(false);
    eofReached_ = false;
    paused_ = false;
    droppedFrames_ = 0;
    mistimedFrames_ = 0;
    decodeErrors_ = 0;
    cache_.ResetMetrics();
    displayWidth_ = 0;
    displayHeight_ = 0;
    videoConfigTracker_.Reset();
    seekSkipPts_.store(-1e18, std::memory_order_release); // no carry-over skip from a prior file's seek
    seekRefreshGeneration_ = 0;
    {
        std::lock_guard lock(mutex_);
        hwDecName_ = "no"; // reset until the video decoder is (re)armed below
        videoClock_.Reset();
        subtitleSeekClockOverrideActive_ = false;
        hasPendingSeek_ = false; // discard any seek queued before this load
        boundaryEofRequested_ = false;
        hasPendingAudioSwitch_ = false;
        hasPendingSubSwitch_ = false;
        externalAudioSeekPending_ = false;
        externalAudioSeekApply_ = false;
    }
    {
        // Drop the previous file's track snapshot up front so a failed open doesn't
        // leave stale entries in the host's track menus.
        std::lock_guard lock(tracksMutex_);
        tracks_.clear();
        selectedAudioId_ = -1;
        selectedSubId_ = -1;
    }
    EmitFlag(PlayerProperty::Pause, false);
    EmitFlag(PlayerProperty::EofReached, false);
    EmitFlag(PlayerProperty::Seeking, false);
    UpdateCoreIdle();

    // Sidecar discovery is pure filesystem work, independent of the demuxer —
    // overlap it with the container open/probe instead of serializing after them.
    // std::async's future blocks in its destructor, so the scan cannot outlive
    // PlayFile even on the early error returns below.
    auto sidecarScan = std::async(
        std::launch::async,
        [path, subs = subAutoLoad_, auds = audioFileAutoLoad_]
        {
            return ScanSidecarFiles(path, subs, auds);
        }
    );

    SessionContext ctx;
    if (!OpenSessionInputs(path, ctx))
    {
        return;
    }
    OpenVideoDecoder(ctx);
    if (!BindSelectedTracks(path, ctx, sidecarScan))
    {
        return;
    }
    PublishLoadedMetadata(path, ctx);

    std::uint64_t initialGeneration = 0;
    {
        std::lock_guard lock(mutex_);
        initialGeneration = nextGeneration_++;
    }
    activeGeneration_.store(initialGeneration, std::memory_order_release);
    // Absolute target for the first generation; NaN starts from the demuxer's
    // current position. Resume is exact.
    PendingSeek seek{
        resumePos > 0.0 ? resumePos : std::numeric_limits<double>::quiet_NaN(), SeekKind::Exact, initialGeneration
    };
    AVPacket* pkt = av_packet_alloc();
    ApplySeekAndPrepareQueues(seek, ctx);
    StartWorkers(ctx);

    for (;;)
    {
        const SessionEndReason reason = RunDemuxSession(ctx, pkt);
        if (reason == SessionEndReason::Stop)
        {
            break;
        }
        if ((reason == SessionEndReason::Eof || reason == SessionEndReason::BoundaryEof) &&
            !HoldAtEndOfFile(ctx, reason == SessionEndReason::BoundaryEof))
        {
            break;
        }
        if (reason == SessionEndReason::Rebind)
        {
            StopWorkers();
            ApplyPendingTrackSwitches(path, ctx);
        }
        // A seek resumes the session — either directly or out of the EOF hold.
        seek = TakePendingSeek();
        activeGeneration_.store(seek.generation, std::memory_order_release);
        QueueEvent(MakeLifecycle(MediaEventType::Seek));
        EmitFlag(PlayerProperty::Seeking, true);
        ApplySeekAndPrepareQueues(seek, ctx);
        if (reason == SessionEndReason::Rebind)
        {
            StartWorkers(ctx);
        }
    }

    StopWorkers();
    CloseSession(ctx, pkt);
}

bool FFmpegPlayer::OpenSessionInputs(const std::string& path, SessionContext& ctx)
{
    InstallFFmpegLogCallback(); // Qt may have clobbered the global callback since construction.
    int openRet = 0;
    {
        PerfScope perf("file-open-input");
        // Opt-in fast probe: cap how much data find_stream_info may read/decode
        // (default is 5 MB / up to 5 s of frames). Well-formed MP4/MKV declare
        // their streams in headers and don't need deep probing; odd containers
        // (TS/AVI) may misdetect under the cap, which is why this is off by
        // default. No fallback reopen — that would double worst-case latency on
        // exactly the files that need the full probe.
        AVDictionary* opts = nullptr;
        if (fastProbe_.load())
        {
            av_dict_set(&opts, "probesize", "1048576", 0);
            av_dict_set(&opts, "analyzeduration", "0", 0);
        }
        openRet = avformat_open_input(&ctx.fmt, path.c_str(), nullptr, opts ? &opts : nullptr);
        av_dict_free(&opts);
    }
    if (openRet < 0)
    {
        Log::Error("FFmpegPlayer: failed to open {}", path);
        QueueEvent(MakeEndFile(ClassifyAvError(openRet)));
        FRAMELIFT_PERF_END("file-load-metadata");
        FRAMELIFT_PERF_END("file-open");
        EmitPlaybackSummary("open-error");
        return false;
    }
    int streamInfoRet = 0;
    {
        PerfScope perf("file-stream-info");
        streamInfoRet = avformat_find_stream_info(ctx.fmt, nullptr);
    }
    if (streamInfoRet < 0)
    {
        Log::Error("FFmpegPlayer: failed to read stream info for {}", path);
        avformat_close_input(&ctx.fmt);
        QueueEvent(MakeEndFile(ClassifyAvError(streamInfoRet)));
        FRAMELIFT_PERF_END("file-load-metadata");
        FRAMELIFT_PERF_END("file-open");
        EmitPlaybackSummary("stream-info-error");
        return false;
    }

    ctx.vIdx = av_find_best_stream(ctx.fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &ctx.vCodec, 0);
    ctx.defaultAudioIdx = av_find_best_stream(ctx.fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    return true;
}

// Video decoder (optional, immutable for this file). On any failure the session
// falls back to audio-only playback; hasVideo_ stays false.
void FFmpegPlayer::OpenVideoDecoder(SessionContext& ctx)
{
    hasVideo_ = false;
    PerfScope perf("video-decoder-open");
    if (ctx.vIdx < 0 || !ctx.vCodec)
    {
        return;
    }
    ctx.vStream = ctx.fmt->streams[ctx.vIdx];
    ctx.vDec = avcodec_alloc_context3(ctx.vCodec);
    if (!ctx.vDec)
    {
        return;
    }
    avcodec_parameters_to_context(ctx.vDec, ctx.vStream->codecpar);
    ctx.vDec->pkt_timebase = ctx.vStream->time_base;
    // Try the selected hardware decode mode; all modes fall back cleanly to
    // software if the codec, device, or renderer interop path is unavailable.
    (void)TryEnableHardwareDecode(ctx.vCodec, ctx.vDec, ctx.hw);
    ctx.vDec->thread_count = ctx.hw.Active() ? 1 : 0; // 0 = auto-detect for software decode
    if (avcodec_open2(ctx.vDec, ctx.vCodec, nullptr) == 0 && ctx.vDec->width > 0 && ctx.vDec->height > 0)
    {
        ctx.W = ctx.vDec->width;
        ctx.H = ctx.vDec->height;
        hasVideo_ = true;
        displayWidth_ = ctx.W;
        displayHeight_ = ctx.H;
        {
            std::lock_guard lock(mutex_);
            hwDecName_ = ctx.hw.Active() ? ctx.hw.DeviceName() : "no";
        }
    }
    else
    {
        Log::Warn("FFmpegPlayer: video decoder unavailable; audio-only playback");
        avcodec_free_context(&ctx.vDec);
        // A cached hw device that fails to open a decoder may be stale (driver
        // reset, GPU gone) — drop it so the next open creates a fresh one.
        av_buffer_unref(&hwDeviceCache_.device);
        hwDeviceCache_.type = -1;
    }
}

bool FFmpegPlayer::BindSelectedTracks(
    const std::string& path, SessionContext& ctx, std::future<std::vector<ExternalSource>>& sidecarScan
)
{
    // Collect the sidecar scan launched at the top of PlayFile (usually finished
    // long ago, hidden behind the container open/probe) and build the track list.
    {
        PerfScope perf("track-discovery");
        auto sources = sidecarScan.get();
        {
            std::lock_guard lock(tracksMutex_);
            externalSources_ = std::move(sources);
        }
        BuildTrackList(ctx.fmt, ctx.defaultAudioIdx);
    }

    // Bind the default audio + subtitle selections.
    {
        PerfScope perf("audio-bind");
        OpenAudioBinding(selectedAudioId_, ctx.fmt, ctx.aud);
    }
    const std::optional<double> containerStart =
        ctx.fmt->start_time == AV_NOPTS_VALUE
            ? std::nullopt
            : std::optional<double>{static_cast<double>(ctx.fmt->start_time) / AV_TIME_BASE};
    const AVStream* primaryStream = hasVideo_ ? ctx.vStream : ctx.aud.stream;
    ctx.timelineStartSec = FFmpegTimeline::SelectStart(containerStart, StreamStartSeconds(primaryStream));
    {
        PerfScope perf("subtitle-bind");
        OpenSubtitleBinding(selectedSubId_, path, ctx.fmt, ctx.timelineStartSec, ctx.subIdx, ctx.sDec, ctx.sStream);
    }

    if (!hasVideo_ && !ctx.aud.dec)
    {
        Log::Error("FFmpegPlayer: failed to open any decoder for {}", path);
        avcodec_free_context(&ctx.vDec);
        OpenAudioBinding(-1, ctx.fmt, ctx.aud); // tears down any partial audio binding
        OpenSubtitleBinding(-1, path, ctx.fmt, ctx.timelineStartSec, ctx.subIdx, ctx.sDec, ctx.sStream);
        avformat_close_input(&ctx.fmt);
        QueueEvent(MakeEndFile(EndFileReason::NoStream));
        FRAMELIFT_PERF_END("file-load-metadata");
        FRAMELIFT_PERF_END("file-open");
        EmitPlaybackSummary("no-stream");
        return false;
    }

    // Something plays, but a present stream may have been dropped because its decoder was
    // unavailable — tell the user so the silent audio-only / video-only fallback isn't a
    // mystery. (The both-failed case returned NoStream above, so these are exclusive.)
    if (ctx.vIdx >= 0 && !hasVideo_ && ctx.aud.dec)
    {
        QueueEvent(MakeNotice(MediaNoticeKind::VideoUnsupported));
    }
    else if (ctx.defaultAudioIdx >= 0 && !ctx.aud.dec && hasVideo_)
    {
        QueueEvent(MakeNotice(MediaNoticeKind::AudioUnsupported));
    }
    return true;
}

void FFmpegPlayer::PublishLoadedMetadata(const std::string& path, SessionContext& ctx)
{
    // Duration (seconds): container first, then the playing stream's own duration.
    double durationSec = 0.0;
    if (ctx.fmt->duration != AV_NOPTS_VALUE && ctx.fmt->duration > 0)
    {
        durationSec = static_cast<double>(ctx.fmt->duration) / AV_TIME_BASE;
    }
    else if (hasVideo_ && ctx.vStream->duration != AV_NOPTS_VALUE)
    {
        durationSec = static_cast<double>(ctx.vStream->duration) * av_q2d(ctx.vStream->time_base);
    }
    else if (ctx.aud.dec && ctx.aud.stream->duration != AV_NOPTS_VALUE)
    {
        durationSec = static_cast<double>(ctx.aud.stream->duration) * av_q2d(ctx.aud.stream->time_base);
    }
    ctx.durationSec = durationSec;
    duration_ = durationSec;

    const AVDictionaryEntry* titleTag = av_dict_get(ctx.fmt->metadata, "title", nullptr, 0);
    {
        std::lock_guard lock(mutex_);
        currentPath_ = path;
        mediaTitle_ = titleTag && titleTag->value ? titleTag->value : std::filesystem::path(path).filename().string();
    }
    QueueEvent(MakeLifecycle(MediaEventType::FileLoaded));
    FRAMELIFT_PERF_END("file-load-metadata");
    EmitDouble(PlayerProperty::Duration, durationSec);
    if (ctx.aud.dec)
    {
        QueueEvent(MakeLifecycle(MediaEventType::AudioReconfig));
    }

    // Still image: single video frame, no audio — held per image-display-duration.
    ctx.isImage = hasVideo_ && !ctx.aud.dec && ctx.vStream->nb_frames == 1;
}

// Apply a pending audio/subtitle track switch (workers are joined here).
void FFmpegPlayer::ApplyPendingTrackSwitches(const std::string& path, SessionContext& ctx)
{
    bool doAudio = false;
    bool doSub = false;
    int64_t audId = -1;
    int64_t subId = -1;
    {
        std::lock_guard lock(mutex_);
        doAudio = hasPendingAudioSwitch_;
        audId = pendingAudioId_;
        hasPendingAudioSwitch_ = false;
        doSub = hasPendingSubSwitch_;
        subId = pendingSubId_;
        hasPendingSubSwitch_ = false;
    }
    if (doAudio)
    {
        OpenAudioBinding(audId, ctx.fmt, ctx.aud);
        if (ctx.aud.dec)
        {
            QueueEvent(MakeLifecycle(MediaEventType::AudioReconfig));
        }
    }
    if (doSub)
    {
        OpenSubtitleBinding(subId, path, ctx.fmt, ctx.timelineStartSec, ctx.subIdx, ctx.sDec, ctx.sStream);
    }
}

// Apply a pending seek, then advance the persistent workers to its generation.
// Codec contexts are deliberately not touched here: each worker consumes a Flush
// control record and flushes the decoder it exclusively owns.
void FFmpegPlayer::ApplySeekAndPrepareQueues(PendingSeek& seek, SessionContext& ctx)
{
    const bool timingSeekApply = !std::isnan(seek.target);
    const std::string seekApplyPerf = SeekPerfName("seek-apply", seek.generation);
    if (timingSeekApply)
    {
        FRAMELIFT_PERF_START(seekApplyPerf.c_str());
    }
    if (!std::isnan(seek.target))
    {
        const SeekPlan plan = BuildSeekPlan(seek.kind, seek.target);
        const auto ts = static_cast<int64_t>(FFmpegTimeline::ToDemux(seek.target, ctx.timelineStartSec) * AV_TIME_BASE);
        // A failed seek leaves the demuxer position untouched. The queue generation
        // still advances because the request already discarded queued packets/audio;
        // the first frame from the continuing demux position refreshes immediately.
        int ret = av_seek_frame(ctx.fmt, -1, ts, plan.backwardFlag ? AVSEEK_FLAG_BACKWARD : 0);
        if (ret < 0 && !plan.backwardFlag)
        {
            // Forward keyframe seeks (no BACKWARD flag) land on the first keyframe at
            // or after the target so a +N step always advances — but near EOF there
            // may be none, and some demuxers can't seek forward at all. Retry landing
            // at/before instead of failing the seek outright.
            ret = av_seek_frame(ctx.fmt, -1, ts, AVSEEK_FLAG_BACKWARD);
        }
        const bool seekSucceeded = ret >= 0;
        if (!seekSucceeded)
        {
            Log::Warn("FFmpegPlayer: seek to {}s failed ({})", seek.target, ret);
        }
        FRAMELIFT_PERF_END(seekApplyPerf.c_str());
        if (ctx.aud.external && ctx.aud.fmt)
        {
            std::lock_guard lock(mutex_);
            externalAudioSeekPending_ = true;
            externalAudioSeekApply_ = seekSucceeded;
            externalAudioSeekGeneration_ = seek.generation;
            externalAudioSeekTarget_ = FFmpegTimeline::ToDemux(seek.target, ctx.aud.startOffset);
        }
        {
            std::lock_guard lock(mutex_);
            videoClock_.Reset();
            // Every applied generation resets the clocks here, so both gates must
            // re-arm: seekSettled_ so the worker re-paints before the next re-seek,
            // and seekClockValid_ so a held repeat anchors to seekTarget_ instead of
            // reading GetMasterClock()==0 and re-targeting from 0.
            seekSettled_ = false;
            seekClockValid_ = false;
            if (seekSucceeded)
            {
                subtitleSeekClockOverride_ = seek.target;
                subtitleSeekClockOverrideActive_ = true;
            }
            else
            {
                subtitleSeekClockOverrideActive_ = false;
            }
        }
        // Exact: discard toward a successful target. On failure, continue from
        // whatever packet the demuxer supplies next without target discard.
        seekSkipPts_.store(seekSucceeded ? plan.skipPts : kSeekNoSkipPts, std::memory_order_release);
        seekRefreshGeneration_.store(seek.generation, std::memory_order_release);
        eofReached_ = false;
        EmitFlag(PlayerProperty::EofReached, false);
        QueueEvent(MakeLifecycle(MediaEventType::PlaybackRestart));
        subtitles_->ForceNextUpdate();
        RequestRender();
        // Clear the pending seek and the UI "seeking" state regardless of outcome,
        // so a failed seek doesn't leave the player stuck mid-seek.
        seek.target = std::numeric_limits<double>::quiet_NaN();
        EmitFlag(PlayerProperty::Seeking, false);
        UpdateCoreIdle();
    }

    // Clear the abort flag before producers enter the new generation. Flush is
    // ordered ahead of packets inside each queue even if a producer runs first.
    cache_.Reset();
    const bool flushDecoders = timingSeekApply;
    audioQ_->BeginGeneration(seek.generation, flushDecoders);
    videoQ_->BeginGeneration(seek.generation, flushDecoders);
    subQ_->BeginGeneration(seek.generation, flushDecoders);

    // Failed seeks still resume cleanly at the demuxer's unchanged position. A
    // newer coalesced request has already changed FFmpegAudioOutput's expected
    // generation, so this completion cannot accidentally reopen stale audio.
    audioOut_->ResumeGeneration(seek.generation);
    cv_.notify_all(); // wake an external-audio worker parked at EOF/interruption
}

FFmpegPlayer::SessionEndReason FFmpegPlayer::RunDemuxSession(SessionContext& ctx, AVPacket* pkt)
{
    // Demux into the current persistent worker generation.
    const std::uint64_t generation = activeGeneration_.load(std::memory_order_acquire);

    // Post-seek refill: time from generation start to the first packet handed to
    // the presented stream's queue.
    bool timingRefill = seekRefreshGeneration_.load(std::memory_order_acquire) == generation;
    const auto refillStarted = std::chrono::steady_clock::now();

    SessionEndReason reason = SessionEndReason::Eof;
    for (;;)
    {
        if (StopRequested())
        {
            reason = SessionEndReason::Stop;
            break;
        }
        {
            std::lock_guard lock(mutex_);
            if (boundaryEofRequested_)
            {
                reason = SessionEndReason::BoundaryEof;
                break;
            }
            if (hasPendingSeek_ && seekSettled_)
            {
                reason = (hasPendingAudioSwitch_ || hasPendingSubSwitch_) ? SessionEndReason::Rebind
                                                                          : SessionEndReason::Seek;
                break;
            }
        }
        if (av_read_frame(ctx.fmt, pkt) < 0)
        {
            reason = SessionEndReason::Eof;
            break;
        }
        bool pushed = true;
        bool presentedStream = false;
        if (hasVideo_ && pkt->stream_index == ctx.vIdx)
        {
            pushed = videoQ_->Push(pkt, generation);
            presentedStream = true;
        }
        else if (ctx.aud.dec && !ctx.aud.external && pkt->stream_index == ctx.aud.streamIndex)
        {
            pushed = audioQ_->Push(pkt, generation);
            presentedStream = !hasVideo_;
        }
        else if (ctx.subIdx >= 0 && pkt->stream_index == ctx.subIdx)
        {
            pushed = subQ_->Push(pkt, generation);
        }
        av_packet_unref(pkt);
        if (timingRefill && presentedStream && pushed)
        {
            LogSeekPerf("seek-refill", generation, refillStarted);
            timingRefill = false;
        }
        if (!pushed)
        {
            if (StopRequested())
            {
                reason = SessionEndReason::Stop;
            }
            else
            {
                std::lock_guard lock(mutex_);
                if (boundaryEofRequested_)
                {
                    reason = SessionEndReason::BoundaryEof;
                }
                else
                {
                    reason = (hasPendingAudioSwitch_ || hasPendingSubSwitch_) ? SessionEndReason::Rebind
                                                                              : SessionEndReason::Seek;
                }
            }
            break;
        }
    }

    if (reason == SessionEndReason::Eof)
    {
        if (hasVideo_)
        {
            videoQ_->SignalEof();
        }
        if (ctx.subIdx >= 0 && ctx.sDec)
        {
            subQ_->SignalEof();
        }
        if (ctx.aud.dec && !ctx.aud.external)
        {
            audioQ_->SignalEof();
        }
        else if (ctx.aud.external)
        {
            audioQ_->Interrupt();
        }

        bool drained = true;
        if (hasVideo_)
        {
            drained = videoQ_->WaitForEof(generation) && drained;
        }
        if (ctx.aud.dec && !ctx.aud.external)
        {
            drained = audioQ_->WaitForEof(generation) && drained;
        }
        if (ctx.subIdx >= 0 && ctx.sDec)
        {
            drained = subQ_->WaitForEof(generation) && drained;
        }
        if (!drained)
        {
            if (StopRequested())
            {
                reason = SessionEndReason::Stop;
            }
            else
            {
                std::lock_guard lock(mutex_);
                if (boundaryEofRequested_)
                {
                    reason = SessionEndReason::BoundaryEof;
                }
                else
                {
                    reason = (hasPendingAudioSwitch_ || hasPendingSubSwitch_) ? SessionEndReason::Rebind
                                                                              : SessionEndReason::Seek;
                }
            }
        }
    }
    else if (reason == SessionEndReason::Seek || reason == SessionEndReason::Rebind)
    {
        // A coalesced seek reaches this path after the prior generation paints;
        // invalidate it now before the next av_seek_frame.
        audioQ_->Interrupt();
        videoQ_->Interrupt();
        subQ_->Interrupt();
    }
    return reason;
}

void FFmpegPlayer::StartWorkers(SessionContext& ctx)
{
    if (ctx.aud.dec)
    {
        const double audioTimestampOffset = ctx.aud.external ? ctx.aud.startOffset : ctx.timelineStartSec;
        audioThread_ = std::thread(&FFmpegPlayer::AudioWorker, this, ctx.aud.dec, ctx.aud.stream, audioTimestampOffset);
    }
    if (hasVideo_)
    {
        videoThread_ = std::thread(
            &FFmpegPlayer::VideoWorker, this, ctx.vDec, ctx.vStream, ctx.W, ctx.H, &ctx.hw, ctx.timelineStartSec
        );
    }
    if (ctx.subIdx >= 0 && ctx.sDec)
    {
        subtitleThread_ = std::thread(&FFmpegPlayer::SubtitleWorker, this, ctx.sDec, ctx.sStream, ctx.timelineStartSec);
    }
    if (ctx.aud.external && ctx.aud.fmt)
    {
        extAudioThread_ = std::thread(&FFmpegPlayer::ExternalAudioDemux, this, ctx.aud.fmt, ctx.aud.streamIndex);
    }
}

void FFmpegPlayer::StopWorkers()
{
    audioQ_->Stop();
    videoQ_->Stop();
    subQ_->Stop();
    cv_.notify_all();
    if (audioThread_.joinable())
    {
        audioThread_.join();
    }
    if (videoThread_.joinable())
    {
        videoThread_.join();
    }
    if (subtitleThread_.joinable())
    {
        subtitleThread_.join();
    }
    if (extAudioThread_.joinable())
    {
        extAudioThread_.join();
    }
}

bool FFmpegPlayer::HoldAtEndOfFile(const SessionContext& ctx, bool boundaryRequested)
{
    // Decide whether this end advances the playlist (EndFile) or just holds.
    bool emitEnd = true;
    {
        std::lock_guard lock(mutex_);
        boundaryRequested = boundaryRequested || boundaryEofRequested_;
    }
    if (!boundaryRequested && ctx.isImage && imageDisplayDuration_.load() <= 0.0)
    {
        // An ordinary still image holds forever, but a forward boundary command
        // must be able to turn that hold into the same clean EOF as video.
        std::unique_lock lock(mutex_);
        cv_.wait(
            lock,
            [this]
            {
                return shutdown_.load() || hasPendingLoad_ || hasPendingSeek_ || stopRequested_ ||
                       boundaryEofRequested_;
            }
        );
        boundaryRequested = boundaryEofRequested_;
        emitEnd = boundaryRequested;
    }
    else if (!boundaryRequested && ctx.isImage)
    {
        // Slideshow still: hold the configured duration unless interrupted.
        const auto until =
            std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                                   std::chrono::duration<double>(imageDisplayDuration_.load())
                                               );
        std::unique_lock lock(mutex_);
        if (cv_.wait_until(
                lock, until,
                [this]
                {
                    return shutdown_.load() || hasPendingLoad_ || hasPendingSeek_ || stopRequested_ ||
                           boundaryEofRequested_;
                }
            ))
        {
            boundaryRequested = boundaryEofRequested_;
            emitEnd = boundaryRequested; // boundary completes; seek/stop interrupts
        }
    }
    if (emitEnd)
    {
        eofReached_ = true;
        EmitFlag(PlayerProperty::EofReached, true);
        UpdateCoreIdle();
        QueueEvent(MakeEndFile(EndFileReason::Eof));
    }

    // keep-open: hold the last frame here until a seek or stop. A playlist that
    // advances on EndFile arrives as a new load (Stop); a manual seek resumes;
    // an end-of-playlist Stop() sets stopRequested_ to break the hold and go idle.
    {
        std::unique_lock lock(mutex_);
        cv_.wait(
            lock,
            [this]
            {
                return shutdown_.load() || hasPendingLoad_ || hasPendingSeek_ || stopRequested_;
            }
        );
    }
    return !StopRequested();
}

void FFmpegPlayer::CloseSession(SessionContext& ctx, AVPacket*& pkt)
{
    av_packet_free(&pkt);
    // Keep the audio device open when another file is already queued (the common playlist
    // advance): the next PlayFile's OpenAudioBinding reuses the still-running QAudioSink
    // when the output format matches, avoiding a device close/reopen per file. Only tear it
    // down when we're genuinely stopping (shutdown), where the next open won't follow.
    bool advancing = false;
    const char* summaryReason = "stop";
    {
        std::lock_guard lock(mutex_);
        advancing = hasPendingLoad_;
        if (shutdown_.load())
        {
            summaryReason = "shutdown";
        }
        else if (hasPendingLoad_)
        {
            summaryReason = "superseded";
        }
        else if (!stopRequested_)
        {
            summaryReason = "ended";
        }
    }
    EmitPlaybackSummary(summaryReason);
    if (!advancing)
    {
        audioOut_->Close();
    }
    avcodec_free_context(&ctx.vDec);
    if (ctx.aud.dec)
    {
        avcodec_free_context(&ctx.aud.dec);
    }
    if (ctx.aud.external && ctx.aud.fmt)
    {
        avformat_close_input(&ctx.aud.fmt);
    }
    if (ctx.sDec)
    {
        avcodec_free_context(&ctx.sDec);
    }
    JoinSubtitlePreload(); // must not outlive the session whose track it feeds
    subtitles_->ClearTrack();
    avformat_close_input(&ctx.fmt);
    {
        std::lock_guard lock(tracksMutex_);
        tracks_.clear();
        externalSources_.clear();
        selectedAudioId_ = -1;
        selectedSubId_ = -1;
    }
}

bool FFmpegPlayer::TryEnableHardwareDecode(const AVCodec* codec, AVCodecContext* dec, FFmpegHwDecode& hw)
{
    if (!hwdec_.load())
    {
        return false;
    }

    const auto tryMode = [&](VideoDecodeMode mode) -> bool
    {
        switch (mode)
        {
        case VideoDecodeMode::VulkanZeroCopy:
#if FRAMELIFT_MODULE_GRAPHICS_VULKAN
            return vulkanZeroCopyAvailable_ && vkHwDevice_ && hw.TryEnableVulkan(codec, dec, vkHwDevice_);
#else
            break;
#endif
        case VideoDecodeMode::Vulkan:
#if !FRAMELIFT_MODULE_GRAPHICS_VULKAN
            break;
#endif
        case VideoDecodeMode::Cuda:
        case VideoDecodeMode::D3D11VA:
        case VideoDecodeMode::DXVA2:
        case VideoDecodeMode::VAAPI:
            return hw.TryEnableBackend(codec, dec, HwBackendFromVideoDecodeMode(mode), &hwDeviceCache_);
        case VideoDecodeMode::Off:
        case VideoDecodeMode::Auto:
            break;
        }
        return false;
    };

    const VideoDecodeMode mode = videoDecodeMode_.load();
    if (mode == VideoDecodeMode::Off)
    {
        return false;
    }
    if (mode != VideoDecodeMode::Auto)
    {
        return tryMode(mode);
    }

    for (const VideoDecodeMode candidate : AutoVideoDecodePreference())
    {
        if (candidate == VideoDecodeMode::Off)
        {
            break;
        }
        if (tryMode(candidate))
        {
            return true;
        }
    }
    return false;
}
