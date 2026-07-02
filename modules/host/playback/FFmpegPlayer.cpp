#include "FFmpegPlayer.h"

#include "FFmpegPlayerInternal.h"

#include "FFmpegAudioFilter.h"
#include "FFmpegAudioOutput.h"
#include "FFmpegClock.h"
#include "FFmpegError.h"
#include "FFmpegFilters.h"
#include "FFmpegHwDecode.h"
#include "FFmpegLetterbox.h"
#include "FFmpegPacketQueue.h"
#include "FFmpegTrackLabel.h"
#if FRAMELIFT_MODULE_GRAPHICS_VULKAN
#include "FFmpegVulkanDevice.h"
#endif

#include "IGraphicsBackend.h"

#include "CacheSettings.h"         // ToReadAheadCacheOptions (host/read-ahead)
#include "FFmpegSettingsMapping.h" // To{PlaybackOptions,VideoDecodeMode,...}
#include "Settings.h"              // host aggregate settings

#include <framelift/Log.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <format>
#include <limits>
#include <utility>

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

// PerfScope / MakeLifecycle / MakeEndFile / MakeNotice / CountDecodeError /
// FFmpegLogCallback / InstallFFmpegLogCallback / FramePtsSec live in
// FFmpegPlayerInternal.h (shared by every FFmpegPlayer*.cpp TU).
using namespace ffplay_detail;

// ── Construction / shutdown ───────────────────────────────────────────────────

FFmpegPlayer::FFmpegPlayer()
    : audioOut_(std::make_unique<FFmpegAudioOutput>()), audioQ_(std::make_unique<FFmpegPacketQueue>()),
      videoQ_(std::make_unique<FFmpegPacketQueue>()), subQ_(std::make_unique<FFmpegPacketQueue>(64)),
      subtitles_(std::make_unique<FFmpegSubtitles>())
{
    // Funnel libav* logging into the host logger. Re-asserted before each open in
    // PlayFile too, since Qt Multimedia clobbers the global callback (see InstallFFmpegLogCallback).
    InstallFFmpegLogCallback();
#if defined(_WIN32)
    // Auto-reset event the video worker waits on alongside its frame-pacing timer;
    // Wake() signals it so pause/seek/load/shutdown interrupt the wait immediately.
    // Created once here (not per-file) so Wake() never races a null handle.
    videoWakeEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
#endif
    // Share one read-ahead budget across the three queues; push a PausedForCache
    // PropertyChange when a decode worker first stalls / last recovers.
    audioQ_->SetBudget(&cache_);
    videoQ_->SetBudget(&cache_);
    subQ_->SetBudget(&cache_);
    cache_.SetStallCallback(
        [this](bool stalling)
        {
            if (stalling)
            {
                FRAMELIFT_PERF_START("cache-stall");
            }
            else
            {
                FRAMELIFT_PERF_END("cache-stall");
            }
            EmitFlag(PlayerProperty::PausedForCache, stalling);
        }
    );
    decodeThread_ = std::thread(&FFmpegPlayer::DecodeThreadMain, this);
}

FFmpegPlayer::~FFmpegPlayer()
{
    {
        std::lock_guard lock(mutex_);
        shutdown_ = true;
    }
    Wake();
    // Unblock any workers still parked on a queue so PlayFile can return.
    audioQ_->Abort();
    videoQ_->Abort();
    subQ_->Abort();
    if (decodeThread_.joinable())
    {
        decodeThread_.join();
    }

    // Zero-copy teardown (#18): destroy the renderer first so its image views over the
    // displayed AVVkFrame are gone before we drop the frame refs and the wrapped device.
    // (The graphics backend/device is owned by the window, destroyed after the player.)
    renderer_.reset();
#if FRAMELIFT_MODULE_GRAPHICS_VULKAN
    if (displayVkFrame_)
    {
        av_frame_free(&displayVkFrame_);
    }
    if (pendingVkFrame_)
    {
        av_frame_free(&pendingVkFrame_);
    }
    if (vkHwDevice_)
    {
        av_buffer_unref(&vkHwDevice_);
    }
#endif
#if defined(_WIN32)
    if (videoWakeEvent_)
    {
        CloseHandle(static_cast<HANDLE>(videoWakeEvent_));
        videoWakeEvent_ = nullptr;
    }
#endif
}

// Wake the decode thread and any worker parked on cv_, and (Windows) interrupt the
// video worker's high-resolution frame-pacing wait via videoWakeEvent_.
void FFmpegPlayer::Wake()
{
    cv_.notify_all();
#if defined(_WIN32)
    if (videoWakeEvent_)
    {
        SetEvent(static_cast<HANDLE>(videoWakeEvent_));
    }
#endif
}

// ── Decode thread ─────────────────────────────────────────────────────────────

void FFmpegPlayer::DecodeThreadMain()
{
    for (;;)
    {
        std::string path;
        double resume = 0.0;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(
                lock,
                [this]
                {
                    return shutdown_.load() || hasPendingLoad_;
                }
            );
            if (shutdown_.load())
            {
                return;
            }
            path = pendingPath_;
            resume = pendingResume_;
            hasPendingLoad_ = false;
            // A load supersedes any prior Stop(): clear it here (rather than only when
            // parking) so this fresh PlayFile isn't immediately seen as stop-requested.
            stopRequested_ = false;
        }

        try
        {
            PlayFile(path, resume);
        }
        catch (const std::exception& e)
        {
            Log::Error("FFmpegPlayer: decode error: {}", e.what());
            QueueEvent(MakeEndFile(EndFileReason::Error));
            FRAMELIFT_PERF_END("file-load-metadata");
            FRAMELIFT_PERF_END("file-open");
            FRAMELIFT_PERF_END("seek");
            EmitPlaybackSummary("decode-error");
        }
    }
}

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
    seekSkipPts_ = -1e18; // no carry-over skip from a prior file's seek
    seekRefresh_ = false;
    {
        std::lock_guard lock(mutex_);
        hwDecName_ = "no"; // reset until the video decoder is (re)armed below
        videoClockSet_ = false;
        subtitleSeekClockOverrideActive_ = false;
        hasPendingSeek_ = false; // discard any seek queued before this load
        hasPendingAudioSwitch_ = false;
        hasPendingSubSwitch_ = false;
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

    AVFormatContext* fmt = nullptr;
    InstallFFmpegLogCallback(); // Qt may have clobbered the global callback since construction.
    int openRet = 0;
    {
        PerfScope perf("file-open-input");
        openRet = avformat_open_input(&fmt, path.c_str(), nullptr, nullptr);
    }
    if (openRet < 0)
    {
        Log::Error("FFmpegPlayer: failed to open {}", path);
        QueueEvent(MakeEndFile(ClassifyAvError(openRet)));
        FRAMELIFT_PERF_END("file-load-metadata");
        FRAMELIFT_PERF_END("file-open");
        EmitPlaybackSummary("open-error");
        return;
    }
    int streamInfoRet = 0;
    {
        PerfScope perf("file-stream-info");
        streamInfoRet = avformat_find_stream_info(fmt, nullptr);
    }
    if (streamInfoRet < 0)
    {
        Log::Error("FFmpegPlayer: failed to read stream info for {}", path);
        avformat_close_input(&fmt);
        QueueEvent(MakeEndFile(ClassifyAvError(streamInfoRet)));
        FRAMELIFT_PERF_END("file-load-metadata");
        FRAMELIFT_PERF_END("file-open");
        EmitPlaybackSummary("stream-info-error");
        return;
    }

    const AVCodec* vCodec = nullptr;
    const int vIdx = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &vCodec, 0);
    const int defaultAudioIdx = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

    // ── Video decoder (optional, immutable for this file) ─────────────────────
    // hw is declared after vDec so it is destroyed *after* avcodec_free_context(&vDec)
    // (the decoder unwinds through the device ctx / get_format on free).
    AVCodecContext* vDec = nullptr;
    FFmpegHwDecode hw;
    AVStream* vStream = nullptr;
    int W = 0;
    int H = 0;
    hasVideo_ = false;
    {
        PerfScope perf("video-decoder-open");
        if (vIdx >= 0 && vCodec)
        {
            vStream = fmt->streams[vIdx];
            vDec = avcodec_alloc_context3(vCodec);
            if (vDec)
            {
                avcodec_parameters_to_context(vDec, vStream->codecpar);
                vDec->pkt_timebase = vStream->time_base;
                // Try the selected hardware decode mode; all modes fall back cleanly to
                // software if the codec, device, or renderer interop path is unavailable.
                (void)TryEnableHardwareDecode(vCodec, vDec, hw);
                vDec->thread_count = hw.Active() ? 1 : 0; // 0 = auto-detect for software decode
                if (avcodec_open2(vDec, vCodec, nullptr) == 0 && vDec->width > 0 && vDec->height > 0)
                {
                    W = vDec->width;
                    H = vDec->height;
                    hasVideo_ = true;
                    displayWidth_ = W;
                    displayHeight_ = H;
                    {
                        std::lock_guard lock(mutex_);
                        hwDecName_ = hw.Active() ? hw.DeviceName() : "no";
                    }
                }
                else
                {
                    Log::Warn("FFmpegPlayer: video decoder unavailable; audio-only playback");
                    avcodec_free_context(&vDec);
                }
            }
        }
    }

    // ── Discover sidecar files and build the audio/subtitle track list ────────
    {
        PerfScope perf("track-discovery");
        ScanExternalSources(path);
        BuildTrackList(fmt, defaultAudioIdx);
    }

    // ── Bind the default audio + subtitle selections ──────────────────────────
    AudioBinding aud;
    {
        PerfScope perf("audio-bind");
        OpenAudioBinding(selectedAudioId_, fmt, aud);
    }

    int subIdx = -1;
    AVCodecContext* sDec = nullptr;
    AVStream* sStream = nullptr;
    {
        PerfScope perf("subtitle-bind");
        OpenSubtitleBinding(selectedSubId_, path, fmt, subIdx, sDec, sStream);
    }

    if (!hasVideo_ && !aud.dec)
    {
        Log::Error("FFmpegPlayer: failed to open any decoder for {}", path);
        avcodec_free_context(&vDec);
        OpenAudioBinding(-1, fmt, aud); // tears down any partial audio binding
        OpenSubtitleBinding(-1, path, fmt, subIdx, sDec, sStream);
        avformat_close_input(&fmt);
        QueueEvent(MakeEndFile(EndFileReason::NoStream));
        FRAMELIFT_PERF_END("file-load-metadata");
        FRAMELIFT_PERF_END("file-open");
        EmitPlaybackSummary("no-stream");
        return;
    }

    // Something plays, but a present stream may have been dropped because its decoder was
    // unavailable — tell the user so the silent audio-only / video-only fallback isn't a
    // mystery. (The both-failed case returned NoStream above, so these are exclusive.)
    if (vIdx >= 0 && !hasVideo_ && aud.dec)
    {
        QueueEvent(MakeNotice(MediaNoticeKind::VideoUnsupported));
    }
    else if (defaultAudioIdx >= 0 && !aud.dec && hasVideo_)
    {
        QueueEvent(MakeNotice(MediaNoticeKind::AudioUnsupported));
    }

    // Duration (seconds): container first, then the playing stream's own duration.
    double durationSec = 0.0;
    if (fmt->duration != AV_NOPTS_VALUE && fmt->duration > 0)
    {
        durationSec = static_cast<double>(fmt->duration) / AV_TIME_BASE;
    }
    else if (hasVideo_ && vStream->duration != AV_NOPTS_VALUE)
    {
        durationSec = static_cast<double>(vStream->duration) * av_q2d(vStream->time_base);
    }
    else if (aud.dec && aud.stream->duration != AV_NOPTS_VALUE)
    {
        durationSec = static_cast<double>(aud.stream->duration) * av_q2d(aud.stream->time_base);
    }
    duration_ = durationSec;

    const AVDictionaryEntry* titleTag = av_dict_get(fmt->metadata, "title", nullptr, 0);
    {
        std::lock_guard lock(mutex_);
        currentPath_ = path;
        mediaTitle_ = titleTag && titleTag->value ? titleTag->value : std::filesystem::path(path).filename().string();
    }
    QueueEvent(MakeLifecycle(MediaEventType::FileLoaded));
    FRAMELIFT_PERF_END("file-load-metadata");
    EmitDouble(PlayerProperty::Duration, durationSec);
    if (aud.dec)
    {
        QueueEvent(MakeLifecycle(MediaEventType::AudioReconfig));
    }

    // Still image: single video frame, no audio — held per image-display-duration.
    const bool isImage = hasVideo_ && !aud.dec && vStream->nb_frames == 1;

    // Why a frame finished playing, decided after the workers are joined.
    enum class Reason
    {
        Eof,  // demuxer reached end of file
        Stop, // shutdown or a new file load
        Seek, // a seek was requested
    };

    // Absolute target for the next session iteration; NaN ⇒ play from here.
    double seekTo = resumePos > 0.0 ? resumePos : std::numeric_limits<double>::quiet_NaN();
    AVPacket* pkt = av_packet_alloc();
    bool stop = false;

    while (!stop)
    {
        // ── Apply a pending audio/subtitle track switch (workers are joined) ───
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
                OpenAudioBinding(audId, fmt, aud);
                if (aud.dec)
                {
                    QueueEvent(MakeLifecycle(MediaEventType::AudioReconfig));
                }
            }
            if (doSub)
            {
                OpenSubtitleBinding(subId, path, fmt, subIdx, sDec, sStream);
            }
        }

        // ── Apply a pending seek (single-threaded here: workers are joined) ────
        const bool timingSeekApply = !std::isnan(seekTo);
        if (timingSeekApply)
        {
            FRAMELIFT_PERF_START("seek-apply");
        }
        if (!std::isnan(seekTo))
        {
            const auto ts = static_cast<int64_t>(seekTo * AV_TIME_BASE);
            // A failed seek leaves the demuxer position untouched. Flushing the
            // decoders / clock anyway would blank the picture for a seek that never
            // happened, so only tear down and refresh when the seek actually landed.
            const int ret = av_seek_frame(fmt, -1, ts, AVSEEK_FLAG_BACKWARD);
            if (ret < 0)
            {
                Log::Warn("FFmpegPlayer: seek to {}s failed ({})", seekTo, ret);
            }
            else
            {
                if (aud.external && aud.fmt && av_seek_frame(aud.fmt, -1, ts, AVSEEK_FLAG_BACKWARD) < 0)
                {
                    Log::Warn("FFmpegPlayer: external audio seek to {}s failed", seekTo);
                }
                if (vDec)
                {
                    avcodec_flush_buffers(vDec);
                }
                if (aud.dec)
                {
                    avcodec_flush_buffers(aud.dec);
                }
                if (sDec)
                {
                    avcodec_flush_buffers(sDec);
                }
                // Embedded subtitles re-decode from the seek point; drop their buffered
                // events. External subs are pre-loaded with absolute times — leave them.
                if (subIdx >= 0)
                {
                    subtitles_->FlushEvents();
                }
                audioOut_->Flush();
                {
                    std::lock_guard lock(mutex_);
                    videoClockSet_ = false;
                    // Every applied seek (the RequestSeek kick *and* a demux-driven
                    // re-seek during a held burst) resets the clocks here, so both gates
                    // must re-arm: seekSettled_ so the worker re-paints before the next
                    // re-seek, and seekClockValid_ so a held repeat anchors to seekTarget_
                    // instead of reading GetMasterClock()==0 and re-targeting from 0.
                    seekSettled_ = false;
                    seekClockValid_ = false;
                    subtitleSeekClockOverride_ = seekTo;
                    subtitleSeekClockOverrideActive_ = true;
                }
                // Exact (hr-seek): drop decoded frames before the target. Keyframe seek
                // (-inf) presents straight from the keyframe the demuxer landed on.
                seekSkipPts_ = hrSeek_.load() ? seekTo : -1e18;
                seekRefresh_ = true;
                eofReached_ = false;
                EmitFlag(PlayerProperty::EofReached, false);
                QueueEvent(MakeLifecycle(MediaEventType::PlaybackRestart));
                subtitles_->ForceNextUpdate();
                RequestRender();
            }
            // Clear the pending seek and the UI "seeking" state regardless of outcome,
            // so a failed seek doesn't leave the player stuck mid-seek.
            seekTo = std::numeric_limits<double>::quiet_NaN();
            EmitFlag(PlayerProperty::Seeking, false);
            UpdateCoreIdle();
        }

        // ── Spawn workers, then demux until EOF / stop / a new seek ────────────
        audioQ_->Flush();
        videoQ_->Flush();
        subQ_->Flush();
        // Clear the read-ahead accounting + abort flag (the queues were aborted on
        // the seek that brought us here) so the demuxer can refill for this read.
        cache_.Reset();
        if (timingSeekApply)
        {
            FRAMELIFT_PERF_END("seek-apply");
        }
        if (aud.dec)
        {
            audioThread_ = std::thread(&FFmpegPlayer::AudioWorker, this, aud.dec, aud.stream, aud.startOffset);
        }
        if (hasVideo_)
        {
            videoThread_ = std::thread(&FFmpegPlayer::VideoWorker, this, vDec, vStream, W, H, &hw);
        }
        if (subIdx >= 0 && sDec)
        {
            subtitleThread_ = std::thread(&FFmpegPlayer::SubtitleWorker, this, sDec, sStream);
        }
        if (aud.external && aud.fmt)
        {
            extAudioThread_ = std::thread(&FFmpegPlayer::ExternalAudioDemux, this, aud.fmt, aud.streamIndex);
        }

        Reason reason = Reason::Eof;
        for (;;)
        {
            if (StopRequested())
            {
                reason = Reason::Stop;
                break;
            }
            {
                std::lock_guard lock(mutex_);
                // Hold off re-seeking until the current seek has painted a frame, so a
                // burst of held-key repeats steps visibly instead of restarting the
                // decoder before anything is shown. seekSettled_ flips on the present.
                if (hasPendingSeek_ && seekSettled_)
                {
                    reason = Reason::Seek;
                    break;
                }
            }
            if (av_read_frame(fmt, pkt) < 0)
            {
                reason = Reason::Eof;
                break;
            }
            bool pushed = true;
            if (hasVideo_ && pkt->stream_index == vIdx)
            {
                pushed = videoQ_->Push(pkt);
            }
            else if (aud.dec && !aud.external && pkt->stream_index == aud.streamIndex)
            {
                pushed = audioQ_->Push(pkt);
            }
            else if (subIdx >= 0 && pkt->stream_index == subIdx)
            {
                pushed = subQ_->Push(pkt);
            }
            av_packet_unref(pkt);
            if (!pushed)
            {
                // A queue was aborted — a stop or seek is pending; re-resolve which.
                reason = StopRequested() ? Reason::Stop : Reason::Seek;
                break;
            }
        }

        // ── Stop the workers (drain on EOF, abort otherwise) and join ──────────
        if (reason == Reason::Eof)
        {
            videoQ_->SignalEof();
            subQ_->SignalEof();
            // External audio runs on its own clock; abort it (and any backlog) at the
            // video container's EOF so its demux thread can't block the join.
            if (aud.external)
            {
                audioQ_->Abort();
            }
            else
            {
                audioQ_->SignalEof();
            }
        }
        else
        {
            audioQ_->Abort();
            videoQ_->Abort();
            subQ_->Abort();
        }
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

        if (reason == Reason::Stop)
        {
            break;
        }
        if (reason == Reason::Seek)
        {
            seekTo = TakePendingSeek();
            QueueEvent(MakeLifecycle(MediaEventType::Seek));
            EmitFlag(PlayerProperty::Seeking, true);
            continue;
        }

        // ── reason == Eof ─────────────────────────────────────────────────────
        // Decide whether this end advances the playlist (EndFile) or just holds.
        bool emitEnd = true;
        if (isImage && imageDisplayDuration_.load() <= 0.0)
        {
            emitEnd = false; // still image, infinite hold — never auto-advances
        }
        else if (isImage)
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
                        return shutdown_.load() || hasPendingLoad_ || hasPendingSeek_ || stopRequested_;
                    }
                ))
            {
                emitEnd = false; // a seek/stop arrived first — handle it below
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
        if (StopRequested())
        {
            break;
        }
        seekTo = TakePendingSeek();
        QueueEvent(MakeLifecycle(MediaEventType::Seek));
        EmitFlag(PlayerProperty::Seeking, true);
    }

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
    avcodec_free_context(&vDec);
    if (aud.dec)
    {
        avcodec_free_context(&aud.dec);
    }
    if (aud.external && aud.fmt)
    {
        avformat_close_input(&aud.fmt);
    }
    if (sDec)
    {
        avcodec_free_context(&sDec);
    }
    subtitles_->ClearTrack();
    avformat_close_input(&fmt);
    {
        std::lock_guard lock(tracksMutex_);
        tracks_.clear();
        externalSources_.clear();
        selectedAudioId_ = -1;
        selectedSubId_ = -1;
    }
}

// ── Decode workers ────────────────────────────────────────────────────────────

void FFmpegPlayer::AudioWorker(AVCodecContext* dec, AVStream* stream, double startOffset)
{
    const AVRational tb = stream->time_base;
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    AVFrame* filtered = av_frame_alloc();
    auto lastEmit = std::chrono::steady_clock::now();

    // Worker-local normalization graph: rebuilt fresh each session (so a seek flushes
    // stale dynaudnorm lookahead) and reconciled in place on a SetAudioNormalize toggle.
    FFmpegAudioFilter filter;
    uint64_t seenGen = ~0ull; // force a reconcile before the first frame
    bool filterActive = false;

    // Queue one output frame (post seek-skip) and drive the audio-only TimePos clock.
    // ptsSec is the frame's start timestamp, already normalised to the 0 origin.
    const auto deliver = [&](AVFrame* f, double ptsSec)
    {
        if (ptsSec < seekSkipPts_) // exact-seek: discard audio before the target
        {
            return;
        }
        const double audioOffsetSec = static_cast<double>(audioSyncOffsetMs_.load()) / 1000.0;
        audioOut_->Feed(f, ptsSec + audioOffsetSec);
        if (audioOut_->HasDevice())
        {
            ClearSubtitleSeekClockOverride();
            std::lock_guard lock(mutex_);
            // The audio clock is the master when a device is open, and this first
            // post-seek Feed re-establishes it (lastQueuedPts_ ≈ target) — so the seek
            // anchor may release now, regardless of whether the video frame has painted.
            seekClockValid_ = true;
            // Audio-only: this delivered frame is also the "presented" signal (no video
            // worker to paint one). With video, the visible settle point is the video
            // frame — don't let audio race ahead and let the video worker bail unpainted.
            if (!hasVideo_)
            {
                seekSettled_ = true;
            }
        }

        // For audio-only files there is no video worker to drive TimePos.
        if (!hasVideo_)
        {
            FRAMELIFT_PERF_END("file-open");
            FRAMELIFT_PERF_END("seek");

            const auto now = std::chrono::steady_clock::now();
            if (now - lastEmit >= std::chrono::milliseconds(250))
            {
                lastEmit = now;
                const double pos = GetMasterClock();
                EmitDouble(PlayerProperty::TimePos, pos);
                if (duration_.load() > 0.0)
                {
                    EmitDouble(PlayerProperty::PercentPos, pos / duration_.load() * 100.0);
                }
            }
        }
    };

    // Pull every ready frame out of the filter graph (its pts is offset like the raw path).
    const auto drainFilter = [&]
    {
        while (filter.Receive(filtered) == 0)
        {
            const double pts = static_cast<double>(filtered->pts) * av_q2d(filter.OutputTimeBase()) - startOffset;
            deliver(filtered, pts);
            av_frame_unref(filtered);
        }
    };

    const auto feedFrames = [&]
    {
        while (avcodec_receive_frame(dec, frame) == 0)
        {
            // Reconcile the graph with the latest SetAudioNormalize request (no seek).
            const uint64_t gen = normalizeGen_.load();
            if (gen != seenGen)
            {
                seenGen = gen;
                AudioNormalizeParams params;
                {
                    std::lock_guard lock(mutex_);
                    params = normalizeParams_;
                }
                filterActive = normalizeEnabled_.load() && filter.Configure(
                                                               dec->sample_rate, dec->ch_layout, dec->sample_fmt, tb,
                                                               BuildAudioNormalizeGraph(params)
                                                           );
                if (!filterActive)
                {
                    filter.Close();
                }
            }

            if (filterActive)
            {
                filter.Send(frame);
                av_frame_unref(frame);
                drainFilter();
            }
            else
            {
                // Normalise external-audio timestamps to a 0 origin so they share the
                // video container's timeline (startOffset is 0 for embedded audio).
                const double pts = FramePtsSec(frame, tb) - startOffset;
                deliver(frame, pts);
                av_frame_unref(frame);
            }
        }
    };

    while (audioQ_->Pop(pkt))
    {
        if (avcodec_send_packet(dec, pkt) == 0)
        {
            feedFrames();
        }
        else
        {
            CountDecodeError(decodeErrors_, dec);
        }
        av_packet_unref(pkt);
    }
    // Drain the decoder on a clean EOF (skip when the queue was aborted).
    if (!audioQ_->Aborted())
    {
        avcodec_send_packet(dec, nullptr);
        feedFrames();
        if (filterActive) // flush the filter's lookahead tail so the end isn't dropped
        {
            filter.Send(nullptr);
            drainFilter();
        }
    }

    av_frame_free(&filtered);
    av_frame_free(&frame);
    av_packet_free(&pkt);
}

void FFmpegPlayer::VideoWorker(AVCodecContext* dec, AVStream* stream, int dstW, int dstH, FFmpegHwDecode* hw)
{
    const AVRational tb = stream->time_base;
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    AVFrame* swFrame = av_frame_alloc(); // reused readback target for hardware frames
    SwsContext* sws = nullptr;
    std::vector<uint8_t> rgba(static_cast<size_t>(dstW) * dstH * 4);
    bool sentReconfig = false;

#if defined(_WIN32)
    // Raise the scheduler tick to 1 ms for the duration of video playback and own a
    // high-resolution waitable timer, so the frame-pacing sleep below is accurate to
    // sub-millisecond instead of overshooting by a full ~15.6 ms default tick (which
    // made nearly every frame present late / "mistimed"). RAII so both are released
    // on every exit path.
    struct WinTimerScope
    {
        HANDLE timer = nullptr;

        WinTimerScope()
        {
            timeBeginPeriod(1);
            timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        }

        ~WinTimerScope()
        {
            if (timer)
            {
                CloseHandle(timer);
            }
            timeEndPeriod(1);
        }

        WinTimerScope(const WinTimerScope&) = delete;
        WinTimerScope& operator=(const WinTimerScope&) = delete;
    } winTimer;
#endif

    // A frame only counts as "mistimed" when it lags the clock by more than half its
    // frame interval; sub-frame pacing jitter (every frame wakes a hair late) is normal
    // and must not be counted. Derived once from the stream's frame rate.
    const AVRational fr = stream->avg_frame_rate.num > 0 ? stream->avg_frame_rate : stream->r_frame_rate;
    const double frameInterval = (fr.num > 0 && fr.den > 0) ? static_cast<double>(fr.den) / fr.num : 0.0;
    const double mistimedTol = frameInterval > 0.0 ? frameInterval * 0.5 : 1.0 / 120.0; // ~8.3ms fallback

    // Scale one frame to RGBA, pace it against the master clock, then hand it to
    // the render thread. Returns true if interrupted (new load / shutdown / seek).
    const auto present = [&](AVFrame* decoded) -> bool
    {
        // Zero-copy Vulkan frames stay on the GPU — no download, handed off as an AVVkFrame
        // ref below. Other hardware frames live in GPU memory and are downloaded to a
        // software frame (carrying pts) before swscale. Software decode leaves f unchanged.
        const bool vkFrame =
#if FRAMELIFT_MODULE_GRAPHICS_VULKAN
            hw && hw->IsVulkanZeroCopy() && decoded->format == hw->HwPixelFormat();
#else
            false;
#endif
        AVFrame* f = decoded;
        if (!vkFrame && hw && hw->Active() && decoded->format == hw->HwPixelFormat())
        {
            f = hw->MapToSoftware(decoded, swFrame);
            if (!f)
            {
                return false; // transfer hiccup: skip this frame, no clock side-effects
            }
        }

        const double framePts = FramePtsSec(f, tb);
        if (framePts < seekSkipPts_) // exact-seek: discard frames before the target
        {
            return false;
        }

        // Emit VideoReconfig on the first frame and whenever the decoded size
        // changes; scale to the native size (the renderer letterboxes on draw).
        if (!sentReconfig || f->width != dstW || f->height != dstH)
        {
            dstW = f->width;
            dstH = f->height;
            displayWidth_ = dstW;
            displayHeight_ = dstH;
            if (!vkFrame)
            {
                rgba.assign(static_cast<size_t>(dstW) * dstH * 4, 0);
            }
            sentReconfig = true;
            QueueEvent(MakeLifecycle(MediaEventType::VideoReconfig));
        }

        // Establish the video-only wall-clock baseline on the first frame.
        if (!audioOut_->HasDevice())
        {
            std::lock_guard lock(mutex_);
            if (!videoClockSet_)
            {
                videoClockSet_ = true;
                videoClockPts_ = framePts;
                videoClockWall_ = std::chrono::steady_clock::now();
                pauseWall_ = videoClockWall_;
                subtitleSeekClockOverrideActive_ = false;
                seekClockValid_ = true; // video wall clock is the master here — anchor may release
            }
        }

        for (;;)
        {
            {
                std::unique_lock lock(mutex_);
                // Hold while paused, but let a post-seek refresh present one frame so
                // the seek target is shown even when paused.
                cv_.wait(
                    lock,
                    [this]
                    {
                        return !paused_.load() || seekRefresh_.load() || shutdown_.load() || hasPendingLoad_ ||
                               hasPendingSeek_ || stopRequested_;
                    }
                );
                // Honour a pending seek only once this one has painted (seekSettled_),
                // so the target frame is shown before the loop bails to re-seek.
                if (shutdown_.load() || hasPendingLoad_ || stopRequested_ || (hasPendingSeek_ && seekSettled_))
                {
                    return true;
                }
            }

            const double master = GetMasterClock();
            const FrameAction action = DecideFrame(framePts, master, kDropThreshold);
            if (action == FrameAction::Drop)
            {
                droppedFrames_.fetch_add(1);
                return false;
            }
            if (action == FrameAction::Present)
            {
                // Count as mistimed only when late by more than half a frame interval —
                // sub-frame jitter from the wake-up is expected, not a dropped slot.
                if (IsMistimedFrame(framePts, master, mistimedTol))
                {
                    mistimedFrames_.fetch_add(1);
                }
                break;
            }

            // Wait: sleep until the frame is due, capped so we re-check pause/stop/seek.
            const double diff = std::min(framePts - master, 0.1);
#if defined(_WIN32)
            if (winTimer.timer && videoWakeEvent_)
            {
                // High-resolution timer (sub-ms accurate, independent of the global
                // tick), interruptible via videoWakeEvent_ which Wake() signals on
                // pause/seek/load/shutdown. Negative due time = relative, 100 ns units.
                LARGE_INTEGER due;
                due.QuadPart = -static_cast<LONGLONG>(diff * 1.0e7);
                if (due.QuadPart >= 0)
                {
                    due.QuadPart = -1; // guard against a zero/positive (absolute) deadline
                }
                SetWaitableTimer(winTimer.timer, &due, 0, nullptr, nullptr, FALSE);
                HANDLE handles[2] = {winTimer.timer, static_cast<HANDLE>(videoWakeEvent_)};
                WaitForMultipleObjects(2, handles, FALSE, INFINITE);
            }
            else
#endif
            {
                const auto slice = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double>(diff)
                );
                std::unique_lock lock(mutex_);
                cv_.wait_for(
                    lock, slice,
                    [this]
                    {
                        return shutdown_.load() || hasPendingLoad_ || hasPendingSeek_ || paused_.load() ||
                               stopRequested_;
                    }
                );
            }
            if (shutdown_.load() || hasPendingLoad_ || stopRequested_ || (hasPendingSeek_ && seekSettled_))
            {
                return true;
            }
        }

#if FRAMELIFT_MODULE_GRAPHICS_VULKAN
        if (vkFrame)
        {
            // Zero-copy: hand a ref'd AVVkFrame to the render thread (no swscale, no copy).
            AVFrame* clone = av_frame_alloc();
            if (clone && av_frame_ref(clone, f) == 0)
            {
                std::lock_guard fl(frameMutex_);
                if (pendingVkFrame_) // drop a still-unconsumed pending frame
                {
                    av_frame_free(&pendingVkFrame_);
                }
                pendingVkFrame_ = clone;
                pendingW_ = dstW;
                pendingH_ = dstH;
                pendingValid_ = true;
                pendingIsVulkan_ = true;
            }
            else if (clone)
            {
                av_frame_free(&clone);
            }
        }
        else
#endif
        {
            sws = sws_getCachedContext(
                sws, f->width, f->height, static_cast<AVPixelFormat>(f->format), dstW, dstH, AV_PIX_FMT_RGBA,
                SWS_BILINEAR, nullptr, nullptr, nullptr
            );
            if (!sws)
            {
                return false;
            }
            if (rgba.size() != static_cast<size_t>(dstW) * dstH * 4)
            {
                rgba.assign(static_cast<size_t>(dstW) * dstH * 4, 0);
            }
            uint8_t* dst[4] = {rgba.data(), nullptr, nullptr, nullptr};
            int dstStride[4] = {dstW * 4, 0, 0, 0};
            sws_scale(sws, f->data, f->linesize, 0, f->height, dst, dstStride);

            std::lock_guard fl(frameMutex_);
            std::swap(rgba, pendingPixels_);
            pendingW_ = dstW;
            pendingH_ = dstH;
            pendingValid_ = true;
#if FRAMELIFT_MODULE_GRAPHICS_VULKAN
            pendingIsVulkan_ = false;
#endif
        }
        newFramePending_ = true;
        RequestRender();

        // Perf timing: the first presented frame ends whichever op is in flight.
        // Each END is a no-op until its matching START, so calling both every frame
        // is safe; a resume-position seek on load never STARTs "seek", so it stays
        // folded into "file-open".
        FRAMELIFT_PERF_END("file-open");
        FRAMELIFT_PERF_END("seek");

        seekRefresh_ = false; // the post-seek frame has been shown
        bool reseek = false;
        {
            // A frame is now on screen for the current seek: the position is live again
            // (anchor for relative seeks) and the decode loop may honour the next pending
            // seek. Set here — not at clock-establish — so a held key paints each step.
            std::lock_guard lock(mutex_);
            seekSettled_ = true;
            reseek = hasPendingSeek_; // a newer target (e.g. held key) arrived mid-seek
        }
        if (reseek)
        {
            // Tear the session down so the decode loop re-seeks to the latest target. The
            // Abort also frees a demuxer parked in a full Push() — without it the loop
            // could hang one step short once the key is released (no more repeats to kick).
            audioQ_->Abort();
            videoQ_->Abort();
            subQ_->Abort();
            Wake();
        }

        EmitDouble(PlayerProperty::TimePos, framePts);
        if (duration_.load() > 0.0)
        {
            EmitDouble(PlayerProperty::PercentPos, framePts / duration_.load() * 100.0);
        }
        return false;
    };

    const auto drain = [&]() -> bool
    {
        while (avcodec_receive_frame(dec, frame) == 0)
        {
            const bool stop = present(frame);
            av_frame_unref(frame);
            if (stop)
            {
                return true;
            }
        }
        return false;
    };

    bool interrupted = false;
    while (!interrupted && videoQ_->Pop(pkt))
    {
        if (avcodec_send_packet(dec, pkt) == 0)
        {
            interrupted = drain();
        }
        else
        {
            CountDecodeError(decodeErrors_, dec);
        }
        av_packet_unref(pkt);
    }
    if (!interrupted && !videoQ_->Aborted())
    {
        avcodec_send_packet(dec, nullptr);
        drain();
    }

    if (sws)
    {
        sws_freeContext(sws);
    }
    av_frame_free(&swFrame);
    av_frame_free(&frame);
    av_packet_free(&pkt);
}

void FFmpegPlayer::SubtitleWorker(AVCodecContext* dec, AVStream* stream)
{
    const AVRational tb = stream->time_base;
    AVPacket* pkt = av_packet_alloc();
    while (subQ_->Pop(pkt))
    {
        subtitles_->ProcessPacket(dec, pkt, tb.num, tb.den);
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
}

void FFmpegPlayer::ExternalAudioDemux(AVFormatContext* fmt, int streamIndex)
{
    AVPacket* pkt = av_packet_alloc();
    for (;;)
    {
        if (StopRequested())
        {
            break;
        }
        {
            std::lock_guard lock(mutex_);
            if (hasPendingSeek_)
            {
                break;
            }
        }
        if (av_read_frame(fmt, pkt) < 0)
        {
            break; // external audio reached its own EOF
        }
        bool pushed = true;
        if (pkt->stream_index == streamIndex)
        {
            pushed = audioQ_->Push(pkt);
        }
        av_packet_unref(pkt);
        if (!pushed)
        {
            break; // audioQ_ aborted — a stop or seek is pending
        }
    }
    av_packet_free(&pkt);
}

// ── Track model ─────────────────────────────────────────────────────────────

void FFmpegPlayer::ScanExternalSources(const std::string& mediaPath)
{
    externalSources_.clear();
    if (!subAutoLoad_ && !audioFileAutoLoad_)
    {
        return;
    }

    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path media(mediaPath);
    const fs::path dir = media.parent_path();
    const std::string stem = media.stem().string();
    if (dir.empty() || stem.empty())
    {
        return;
    }

    const auto lower = [](std::string s)
    {
        for (char& c : s)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return s;
    };
    const std::string stemL = lower(stem);
    const std::string mediaName = media.filename().string();

    static constexpr std::array<const char*, 4> kSubExt = {".srt", ".ass", ".ssa", ".sub"};
    static constexpr std::array<const char*, 6> kAudExt = {".mka", ".m4a", ".aac", ".ac3", ".dts", ".flac"};

    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
    {
        if (!it->is_regular_file(ec))
        {
            continue;
        }
        const fs::path p = it->path();
        if (p.filename().string() == mediaName)
        {
            continue; // skip the media file itself
        }
        const std::string nameL = lower(p.filename().string());
        if (nameL.find(stemL) == std::string::npos)
        {
            continue; // fuzzy match: sidecar name must contain the media stem
        }
        const std::string ext = lower(p.extension().string());
        const auto matches = [&ext](const auto& list)
        {
            return std::find_if(
                       list.begin(), list.end(),
                       [&](const char* e)
                       {
                           return ext == e;
                       }
                   ) != list.end();
        };
        if (subAutoLoad_ && matches(kSubExt))
        {
            externalSources_.push_back({p.string(), false});
        }
        else if (audioFileAutoLoad_ && matches(kAudExt))
        {
            externalSources_.push_back({p.string(), true});
        }
    }
}

void FFmpegPlayer::BuildTrackList(AVFormatContext* mainFmt, int defaultAudioStream)
{
    std::lock_guard lock(tracksMutex_);
    tracks_.clear();
    nextTrackId_ = 1;

    int64_t defaultAudio = -1;
    int64_t defaultSub = -1;         // an embedded sub flagged DEFAULT
    int64_t defaultSubFallback = -1; // first subtitle track of any kind
    int audioOrd = 0;
    int subOrd = 0;

    // User track-selection preferences (guarded by tracksMutex_, held here).
    const std::string prefLang = subtitleStyle_.preferredLang;
    const std::string audioPrefLang = audioPrefs_.preferredLang;
    const bool preferForced = subtitleStyle_.preferForced;
    int64_t langAudio = -1;     // first audio stream matching the preferred language
    int64_t langSub = -1;       // first sub matching the preferred language
    int64_t forcedSub = -1;     // first forced sub
    int64_t forcedLangSub = -1; // first forced sub matching the preferred language

    // Case-insensitive language match tolerant of 2- vs 3-letter codes ("en"~"eng").
    const auto langMatches = [](const std::string& wanted, const char* tag) -> bool
    {
        if (wanted.empty() || !tag)
        {
            return false;
        }
        const size_t n = std::min(wanted.size(), std::strlen(tag));
        for (size_t i = 0; i < n; ++i)
        {
            if (std::tolower(static_cast<unsigned char>(tag[i])) != std::tolower(static_cast<unsigned char>(wanted[i])))
            {
                return false;
            }
        }
        return n > 0;
    };

    // Embedded audio + subtitle streams, in container order.
    for (unsigned i = 0; i < mainFmt->nb_streams; ++i)
    {
        AVStream* st = mainFmt->streams[i];
        const AVMediaType type = st->codecpar->codec_type;
        if (type != AVMEDIA_TYPE_AUDIO && type != AVMEDIA_TYPE_SUBTITLE)
        {
            continue;
        }
        const AVDictionaryEntry* titleTag = av_dict_get(st->metadata, "title", nullptr, 0);
        const AVDictionaryEntry* langTag = av_dict_get(st->metadata, "language", nullptr, 0);

        TrackEntry e;
        e.id = nextTrackId_++;
        e.kind = type == AVMEDIA_TYPE_AUDIO ? TrackKind::Audio : TrackKind::Subtitle;
        e.container = 0;
        e.streamIndex = static_cast<int>(i);
        e.external = false;
        const int ord = type == AVMEDIA_TYPE_AUDIO ? ++audioOrd : ++subOrd;
        char label[256];
        MakeTrackLabel(label, titleTag ? titleTag->value : nullptr, langTag ? langTag->value : nullptr, ord, nullptr);
        e.label = label;
        e.language = langTag ? langTag->value : "";

        if (type == AVMEDIA_TYPE_AUDIO && static_cast<int>(i) == defaultAudioStream)
        {
            defaultAudio = e.id;
        }
        if (type == AVMEDIA_TYPE_AUDIO && langAudio < 0 &&
            langMatches(audioPrefLang, langTag ? langTag->value : nullptr))
        {
            langAudio = e.id;
        }
        if (type == AVMEDIA_TYPE_SUBTITLE)
        {
            if (defaultSubFallback < 0)
            {
                defaultSubFallback = e.id;
            }
            if ((st->disposition & AV_DISPOSITION_DEFAULT) != 0 && defaultSub < 0)
            {
                defaultSub = e.id;
            }
            const bool forced = (st->disposition & AV_DISPOSITION_FORCED) != 0;
            const bool langOk = langMatches(prefLang, langTag ? langTag->value : nullptr);
            if (langOk && langSub < 0)
            {
                langSub = e.id;
            }
            if (forced && forcedSub < 0)
            {
                forcedSub = e.id;
            }
            if (forced && langOk && forcedLangSub < 0)
            {
                forcedLangSub = e.id;
            }
        }
        tracks_.push_back(std::move(e));
    }

    if (defaultAudio < 0)
    {
        for (const TrackEntry& t : tracks_)
        {
            if (t.kind == TrackKind::Audio)
            {
                defaultAudio = t.id;
                break;
            }
        }
    }

    // External sidecar files (their container index is externalSources_ index + 1).
    for (std::size_t k = 0; k < externalSources_.size(); ++k)
    {
        const ExternalSource& src = externalSources_[k];
        const std::string base = std::filesystem::path(src.path).filename().string();
        TrackEntry e;
        e.id = nextTrackId_++;
        e.kind = src.isAudio ? TrackKind::Audio : TrackKind::Subtitle;
        e.container = static_cast<int>(k) + 1;
        e.streamIndex = -1;
        e.external = true;
        char label[256];
        MakeTrackLabel(label, nullptr, nullptr, 0, base.c_str());
        e.label = label;
        if (!src.isAudio && defaultSubFallback < 0)
        {
            defaultSubFallback = e.id;
        }
        tracks_.push_back(std::move(e));
    }

    selectedAudioId_ = langAudio >= 0 ? langAudio : defaultAudio;

    // Selection precedence: forced (matching language first) when preferForced, then a
    // preferred-language match, then the file's DEFAULT-flagged sub, then any sub.
    int64_t chosenSub = -1;
    if (preferForced)
    {
        chosenSub = forcedLangSub >= 0 ? forcedLangSub : forcedSub;
    }
    if (chosenSub < 0)
    {
        chosenSub = langSub;
    }
    if (chosenSub < 0)
    {
        chosenSub = defaultSub >= 0 ? defaultSub : defaultSubFallback;
    }
    selectedSubId_ = chosenSub;
    for (TrackEntry& t : tracks_)
    {
        t.selected = (t.kind == TrackKind::Audio && t.id == selectedAudioId_) ||
                     (t.kind == TrackKind::Subtitle && t.id == selectedSubId_);
    }
}

void FFmpegPlayer::RefreshSelectedFlags()
{
    std::lock_guard lock(tracksMutex_);
    for (TrackEntry& t : tracks_)
    {
        t.selected = (t.kind == TrackKind::Audio && t.id == selectedAudioId_) ||
                     (t.kind == TrackKind::Subtitle && t.id == selectedSubId_);
    }
}

bool FFmpegPlayer::FindTrack(int64_t id, TrackEntry& out) const
{
    std::lock_guard lock(tracksMutex_);
    for (const TrackEntry& t : tracks_)
    {
        if (t.id == id)
        {
            out = t;
            return true;
        }
    }
    return false;
}

bool FFmpegPlayer::OpenAudioBinding(int64_t id, AVFormatContext* mainFmt, AudioBinding& aud)
{
    // Tear down any existing binding (close external context + audio device).
    if (aud.dec)
    {
        avcodec_free_context(&aud.dec);
    }
    if (aud.external && aud.fmt)
    {
        avformat_close_input(&aud.fmt);
    }
    aud = AudioBinding{};
    // Note: the audio device is NOT closed here. FFmpegAudioOutput::Open() reuses a
    // still-running sink when the new track's output format matches, so on the common
    // file→file boundary the QAudioSink + its thread stay up. Every path below that ends
    // without a live binding closes the device explicitly instead.

    TrackEntry e;
    if (id < 0 || !FindTrack(id, e) || e.kind != TrackKind::Audio)
    {
        audioOut_->Close();
        return false;
    }

    AVFormatContext* srcFmt = nullptr;
    int streamIdx = -1;
    if (!e.external)
    {
        srcFmt = mainFmt;
        streamIdx = e.streamIndex;
    }
    else
    {
        const std::string& srcPath = externalSources_[e.container - 1].path;
        InstallFFmpegLogCallback(); // re-assert; Qt may have clobbered the global callback.
        if (avformat_open_input(&srcFmt, srcPath.c_str(), nullptr, nullptr) < 0)
        {
            Log::Warn("FFmpegPlayer: failed to open external audio {}", srcPath);
            audioOut_->Close();
            return false;
        }
        if (avformat_find_stream_info(srcFmt, nullptr) < 0)
        {
            avformat_close_input(&srcFmt);
            audioOut_->Close();
            return false;
        }
        streamIdx = av_find_best_stream(srcFmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (streamIdx < 0)
        {
            avformat_close_input(&srcFmt);
            audioOut_->Close();
            return false;
        }
    }

    AVStream* st = srcFmt->streams[streamIdx];
    const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
    AVCodecContext* dec = codec ? avcodec_alloc_context3(codec) : nullptr;
    if (!dec)
    {
        if (e.external)
        {
            avformat_close_input(&srcFmt);
        }
        audioOut_->Close();
        return false;
    }
    avcodec_parameters_to_context(dec, st->codecpar);
    dec->pkt_timebase = st->time_base;
    if (avcodec_open2(dec, codec, nullptr) != 0 || !audioOut_->Open(dec->sample_rate, dec->ch_layout, dec->sample_fmt))
    {
        Log::Warn("FFmpegPlayer: audio decoder/output unavailable for track {}", id);
        avcodec_free_context(&dec);
        audioOut_->Close();
        if (e.external)
        {
            avformat_close_input(&srcFmt);
        }
        return false;
    }
    audioOut_->SetVolume(volume_);
    audioOut_->SetMute(muteEnabled_);

    aud.fmt = srcFmt;
    aud.dec = dec;
    aud.stream = st;
    aud.streamIndex = streamIdx;
    aud.external = e.external;
    aud.startOffset = e.external && st->start_time != AV_NOPTS_VALUE
                          ? static_cast<double>(st->start_time) * av_q2d(st->time_base)
                          : 0.0;

    {
        std::lock_guard lock(tracksMutex_);
        selectedAudioId_ = id;
    }
    RefreshSelectedFlags();
    return true;
}

void FFmpegPlayer::OpenSubtitleBinding(
    int64_t id, const std::string& mediaPath, AVFormatContext* mainFmt, int& subIdx, AVCodecContext*& sDec,
    AVStream*& sStream
)
{
    if (sDec)
    {
        avcodec_free_context(&sDec);
    }
    sDec = nullptr;
    sStream = nullptr;
    subIdx = -1;
    subtitles_->ClearTrack();

    {
        std::lock_guard lock(tracksMutex_);
        selectedSubId_ = id;
    }
    RefreshSelectedFlags();

    TrackEntry e;
    if (id < 0 || !subtitles_->Ok() || !FindTrack(id, e) || e.kind != TrackKind::Subtitle)
    {
        return; // subtitles off / unavailable
    }

    if (e.external)
    {
        // External sidecar: pre-load all events; no embedded routing or worker.
        subtitles_->LoadExternalFile(externalSources_[e.container - 1].path.c_str());
        return;
    }

    // Embedded subtitles need the same absolute-event model as sidecars so a seek
    // can render the active cue immediately, even when that cue began before the
    // seek target. Use a separate input so the playback demuxer stays untouched.
    if (subtitles_->LoadEmbeddedStream(mediaPath.c_str(), e.streamIndex))
    {
        return;
    }

    // Fallback: live-decode embedded packets if the separate preload cannot open.
    AVStream* st = mainFmt->streams[e.streamIndex];
    const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
    AVCodecContext* dec = codec ? avcodec_alloc_context3(codec) : nullptr;
    if (!dec)
    {
        return;
    }
    avcodec_parameters_to_context(dec, st->codecpar);
    dec->pkt_timebase = st->time_base;
    if (avcodec_open2(dec, codec, nullptr) != 0)
    {
        avcodec_free_context(&dec);
        return;
    }
    subtitles_->BeginTrack(dec->subtitle_header, dec->subtitle_header_size);
    sDec = dec;
    sStream = st;
    subIdx = e.streamIndex;
}

// ── Clocks ─────────────────────────────────────────────────────────────────────

double FFmpegPlayer::GetMasterClock()
{
    if (audioOut_->HasDevice())
    {
        return audioOut_->MasterClock();
    }
    return VideoWallClock();
}

double FFmpegPlayer::GetSubtitleRenderClock()
{
    const double master = GetMasterClock();
    std::lock_guard lock(mutex_);
    return SelectSubtitleRenderClock(master, subtitleSeekClockOverrideActive_, subtitleSeekClockOverride_);
}

void FFmpegPlayer::ClearSubtitleSeekClockOverride()
{
    std::lock_guard lock(mutex_);
    subtitleSeekClockOverrideActive_ = false;
}

double FFmpegPlayer::VideoWallClock()
{
    std::lock_guard lock(mutex_);
    if (!videoClockSet_)
    {
        return 0.0;
    }
    const auto ref = paused_.load() ? pauseWall_ : std::chrono::steady_clock::now();
    return videoClockPts_ + std::chrono::duration<double>(ref - videoClockWall_).count();
}

// ── Decode-thread helpers ─────────────────────────────────────────────────────

void FFmpegPlayer::QueueEvent(const MediaEvent& e)
{
    void (*fn)(void*) = nullptr;
    void* ud = nullptr;
    {
        std::lock_guard lock(mutex_);
        events_.push(e);
        fn = wakeupCb_.fn;
        ud = wakeupCb_.ud;
    }
    if (fn)
    {
        fn(ud);
    }
}

void FFmpegPlayer::RequestRender()
{
    void (*fn)(void*) = nullptr;
    void* ud = nullptr;
    {
        std::lock_guard lock(mutex_);
        fn = renderCb_.fn;
        ud = renderCb_.ud;
    }
    if (fn)
    {
        fn(ud);
    }
}

void FFmpegPlayer::EmitPlaybackSummary(const char* reason)
{
    if (!Log::PerfActive())
    {
        return;
    }

    std::string hwDec;
    {
        std::lock_guard lock(mutex_);
        hwDec = hwDecName_.empty() ? std::string("unknown") : hwDecName_;
    }

    Log::Emit(
        Log::Level::Perf,
        std::format(
            "playback-summary reason={} video={} duration_ms={:.0f} display={}x{} hwdec={} dropped={} mistimed={} "
            "decode_errors={} cache_hits={} cache_misses={} cache_peak_kib={}",
            reason ? reason : "unknown", hasVideo_ ? 1 : 0, duration_.load() * 1000.0, displayWidth_.load(),
            displayHeight_.load(), hwDec, droppedFrames_.load(), mistimedFrames_.load(), decodeErrors_.load(),
            cache_.Hits(), cache_.Misses(), cache_.PeakUsedKB()
        )
    );
}

void FFmpegPlayer::EmitFlag(PlayerProperty prop, bool value)
{
    if (!observed_[static_cast<std::size_t>(prop)].load())
    {
        return;
    }
    MediaEvent e;
    e.type = MediaEventType::PropertyChange;
    e.property.prop = prop;
    e.property.type = PropertyType::Flag;
    e.property.value.flag = value ? 1 : 0;
    QueueEvent(e);
}

void FFmpegPlayer::EmitDouble(PlayerProperty prop, double value)
{
    if (!observed_[static_cast<std::size_t>(prop)].load())
    {
        return;
    }
    MediaEvent e;
    e.type = MediaEventType::PropertyChange;
    e.property.prop = prop;
    e.property.type = PropertyType::Double;
    e.property.value.dbl = value;
    QueueEvent(e);
}

void FFmpegPlayer::SetIdle(bool idle)
{
    if (idle_.exchange(idle) == idle)
    {
        return; // unchanged
    }
    EmitFlag(PlayerProperty::IdleActive, idle);
    UpdateCoreIdle();
}

void FFmpegPlayer::UpdateCoreIdle()
{
    const bool ci = paused_.load() || eofReached_.load() || idle_.load();
    if (coreIdle_.exchange(ci) != ci)
    {
        EmitFlag(PlayerProperty::CoreIdle, ci);
    }
}

bool FFmpegPlayer::StopRequested()
{
    std::lock_guard lock(mutex_);
    return shutdown_.load() || hasPendingLoad_ || stopRequested_;
}

double FFmpegPlayer::TakePendingSeek()
{
    std::lock_guard lock(mutex_);
    hasPendingSeek_ = false;
    // The committed seek's Flush() is about to zero the master clock; clear the
    // anchor-release gate in the same lock so a held-key Seek() can't read a
    // momentarily-0 GetMasterClock() and re-target from the start.
    seekClockValid_ = false;
    return seekTarget_;
}

bool FFmpegPlayer::TryEnableHardwareDecode(const AVCodec* codec, AVCodecContext* dec, FFmpegHwDecode& hw)
{
    if (!hwdec_.load())
    {
        return false;
    }

    const auto tryMode = [&](VideoDecodeMode mode, bool warnUnavailable) -> bool
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
            return hw.TryEnableBackend(codec, dec, HwBackendFromVideoDecodeMode(mode));
        case VideoDecodeMode::CudaZeroCopy:
            return hw.TryEnableCudaZeroCopy(codec, dec, warnUnavailable);
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
        return tryMode(mode, true);
    }

    for (const VideoDecodeMode candidate : AutoVideoDecodePreference())
    {
        if (candidate == VideoDecodeMode::Off)
        {
            break;
        }
#if FRAMELIFT_MODULE_GRAPHICS_VULKAN
        // Vulkan video decode faults on the NVIDIA driver; let Auto fall through to NVDEC.
        if (candidate == VideoDecodeMode::VulkanZeroCopy && vulkanAdapterIsNvidia_)
        {
            continue;
        }
#endif
        if (tryMode(candidate, false))
        {
            return true;
        }
    }
    return false;
}

// ── Subtitle / audio tracks ───────────────────────────────────────────────────

void FFmpegPlayer::SetAudioPreferences(const AudioPreferences& prefs) noexcept
{
    AudioPreferences old;
    {
        std::lock_guard lock(tracksMutex_);
        old = audioPrefs_;
        audioPrefs_ = prefs; // preferred language is read by BuildTrackList on the decode thread
    }

    audioSyncOffsetMs_ = prefs.syncOffsetMs;
    const bool outputChanged =
        std::strcmp(old.outputDevice, prefs.outputDevice) != 0 || old.channelMode != prefs.channelMode;
    volume_ = std::clamp(prefs.defaultVolume, 0, 100);
    audioOut_->SetPreferences(prefs);
    audioOut_->SetVolume(volume_);
    EmitDouble(PlayerProperty::Volume, static_cast<double>(volume_));

    if (outputChanged && audioOut_->HasDevice() && !idle_.load())
    {
        const double target = ClampSeekTarget(GetMasterClock(), duration_.load());
        RequestSeek(target);
        audioQ_->Abort();
        videoQ_->Abort();
        subQ_->Abort();
    }
}

AudioPreferences FFmpegPlayer::GetAudioPreferences() const noexcept
{
    std::lock_guard lock(tracksMutex_);
    return audioPrefs_;
}

void FFmpegPlayer::CycleSubtitleTrack() noexcept
{
    // Advance off → first → … → last → off, then apply via the switch path.
    int64_t next = -1;
    {
        std::lock_guard lock(tracksMutex_);
        std::vector<int64_t> subs;
        for (const TrackEntry& t : tracks_)
        {
            if (t.kind == TrackKind::Subtitle)
            {
                subs.push_back(t.id);
            }
        }
        if (!subs.empty())
        {
            const auto it = std::find(subs.begin(), subs.end(), selectedSubId_);
            if (it == subs.end())
            {
                next = subs.front();
            }
            else
            {
                const auto nextIt = it + 1;
                next = nextIt == subs.end() ? -1 : *nextIt;
            }
        }
    }
    SelectSubtitleTrack(next);
}

void FFmpegPlayer::EnumerateSubtitleTracks(void (*visit)(const SubtitleTrack*, void*), void* ud) const noexcept
{
    if (!visit)
    {
        return;
    }
    std::lock_guard lock(tracksMutex_);
    for (const TrackEntry& t : tracks_)
    {
        if (t.kind != TrackKind::Subtitle)
        {
            continue;
        }
        SubtitleTrack s{};
        s.id = t.id;
        s.selected = t.selected;
        std::snprintf(s.label, sizeof(s.label), "%s", t.label.c_str());
        visit(&s, ud);
    }
}

void FFmpegPlayer::SelectSubtitleTrack(int64_t id) noexcept
{
    if (idle_.load())
    {
        return;
    }
    // Force a seek-to-current so the decode thread rebuilds the subtitle binding at
    // the seek boundary (re-feeding events for an embedded track from this point).
    const double target = ClampSeekTarget(GetMasterClock(), duration_.load());
    {
        std::lock_guard lock(mutex_);
        pendingSubId_ = id;
        hasPendingSubSwitch_ = true;
        seekTarget_ = target;
        hasPendingSeek_ = true;
    }
    audioQ_->Abort();
    videoQ_->Abort();
    subQ_->Abort();
    Wake();
}

void FFmpegPlayer::EnumerateAudioTracks(void (*visit)(const AudioTrack*, void*), void* ud) const noexcept
{
    if (!visit)
    {
        return;
    }
    std::lock_guard lock(tracksMutex_);
    for (const TrackEntry& t : tracks_)
    {
        if (t.kind != TrackKind::Audio)
        {
            continue;
        }
        AudioTrack a{};
        a.id = t.id;
        a.selected = t.selected;
        std::snprintf(a.label, sizeof(a.label), "%s", t.label.c_str());
        visit(&a, ud);
    }
}

void FFmpegPlayer::SelectAudioTrack(int64_t id) noexcept
{
    if (idle_.load())
    {
        return;
    }
    const double target = ClampSeekTarget(GetMasterClock(), duration_.load());
    {
        std::lock_guard lock(mutex_);
        pendingAudioId_ = id;
        hasPendingAudioSwitch_ = true;
        seekTarget_ = target;
        hasPendingSeek_ = true;
    }
    audioQ_->Abort();
    videoQ_->Abort();
    subQ_->Abort();
    Wake();
}

// ── Events ────────────────────────────────────────────────────────────────────

MediaEvent FFmpegPlayer::PollEvent() noexcept
{
    std::lock_guard lock(mutex_);
    if (events_.empty())
    {
        return MediaEvent{}; // type == MediaEventType::None
    }
    const MediaEvent e = events_.front();
    events_.pop();
    return e;
}

void FFmpegPlayer::SetWakeupCallback(void (*cb)(void*), void* ud) noexcept
{
    std::lock_guard lock(mutex_);
    wakeupCb_ = {cb, ud};
}

