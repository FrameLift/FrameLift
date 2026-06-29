#include "FFmpegPlayer.h"

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
#include <limits>
#include <utility>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libavutil/rational.h>
#include <libswscale/swscale.h>
}

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

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX // keep std::min/std::max usable below
#endif
#include <timeapi.h> // timeBeginPeriod / timeEndPeriod (winmm)
#include <windows.h>
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002 // Win10 1803+; define for older SDK headers
#endif
#endif

namespace
{
MediaEvent MakeLifecycle(MediaEventType type)
{
    MediaEvent e;
    e.type = type;
    return e;
}

MediaEvent MakeEndFile(EndFileReason reason)
{
    MediaEvent e;
    e.type = MediaEventType::EndFile;
    e.endReason = reason;
    return e;
}

// A non-fatal notice (playback continues): the kind rides in property.value.i64, which
// PollEvent copies by value like any other field. The host maps it to a notification.
MediaEvent MakeNotice(MediaNoticeKind kind)
{
    MediaEvent e;
    e.type = MediaEventType::Notice;
    e.property.type = PropertyType::Int64;
    e.property.value.i64 = static_cast<int64_t>(kind);
    return e;
}

// A packet failed to decode: bump the counter and warn, but throttle the log so a
// badly corrupt stream can't flood it (first error, then every 100th).
void CountDecodeError(std::atomic<int64_t>& counter, const AVCodecContext* dec)
{
    const int64_t n = counter.fetch_add(1) + 1;
    if (n == 1 || n % 100 == 0)
    {
        Log::Warn("FFmpegPlayer: {} decode error (total {})", av_get_media_type_string(dec->codec_type), n);
    }
}

// Route FFmpeg's libav* logging through the host logger (mirrors AssLogCallback for
// libass). FFmpeg's default callback writes raw to stderr, bypassing our message
// pattern *and* the IsSuppressed filter in Log.cpp; routing it through Log::* gives
// it timestamps/colors, lands it in the Log Viewer buffer, and lets IsSuppressed drop
// known-benign container noise (UDTA/chapter/codec-params) from stderr while keeping it
// in the buffer. Only messages at or below the active av_log level reach us (default
// AV_LOG_INFO), so verbose/debug spam never arrives unless explicitly enabled.
void FFmpegLogCallback(void* /*avcl*/, int level, const char* fmt, va_list va)
{
    // A custom av_log callback is invoked for *every* message regardless of level —
    // FFmpeg's level threshold is applied inside its default callback, not before
    // dispatch. So we must gate on av_log_get_level() ourselves, or libav's VERBOSE/
    // DEBUG internals (CUDA symbol loads, FFT transform-tree dumps, ...) flood the log.
    if (level > av_log_get_level())
    {
        return;
    }
    char buf[1024];
    std::vsnprintf(buf, sizeof(buf), fmt, va);
    // FFmpeg messages usually carry a trailing newline; strip it so Log::* doesn't
    // emit a blank line after each one.
    for (size_t n = std::strlen(buf); n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'); --n)
    {
        buf[n - 1] = '\0';
    }
    if (buf[0] == '\0')
    {
        return;
    }
    if (level <= AV_LOG_ERROR)
    {
        Log::Error("FFmpeg: {}", buf);
    }
    else if (level <= AV_LOG_WARNING)
    {
        Log::Warn("FFmpeg: {}", buf);
    }
    else
    {
        Log::Info("FFmpeg: {}", buf);
    }
}

// (Re)install our av_log callback. av_log_set_callback is global libav state, and on
// Linux the Qt Multimedia FFmpeg backend (loaded lazily via QMediaDevices/QAudioSink)
// shares the same libav* and clobbers it with its own handler — so a one-shot install
// in the constructor doesn't stick. Re-assert it right before each open instead; the
// call is cheap (just a function-pointer store) and idempotent.
void InstallFFmpegLogCallback()
{
    av_log_set_callback(&FFmpegLogCallback);
}

// Presentation timestamp of a decoded frame in seconds (0 when unknown).
double FramePtsSec(const AVFrame* f, AVRational tb)
{
    int64_t pts = f->best_effort_timestamp;
    if (pts == AV_NOPTS_VALUE)
    {
        pts = f->pts;
    }
    return pts == AV_NOPTS_VALUE ? 0.0 : static_cast<double>(pts) * av_q2d(tb);
}
} // namespace

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
        }

        try
        {
            PlayFile(path, resume);
        }
        catch (const std::exception& e)
        {
            Log::Error("FFmpegPlayer: decode error: {}", e.what());
            QueueEvent(MakeEndFile(EndFileReason::Error));
        }
    }
}

void FFmpegPlayer::PlayFile(const std::string& path, double resumePos)
{
    FRAMELIFT_PERF_START("file-open");
    QueueEvent(MakeLifecycle(MediaEventType::StartFile));
    SetIdle(false);
    eofReached_ = false;
    paused_ = false;
    droppedFrames_ = 0;
    mistimedFrames_ = 0;
    decodeErrors_ = 0;
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
    if (const int ret = avformat_open_input(&fmt, path.c_str(), nullptr, nullptr); ret < 0)
    {
        Log::Error("FFmpegPlayer: failed to open {}", path);
        QueueEvent(MakeEndFile(ClassifyAvError(ret)));
        return;
    }
    if (const int ret = avformat_find_stream_info(fmt, nullptr); ret < 0)
    {
        Log::Error("FFmpegPlayer: failed to read stream info for {}", path);
        avformat_close_input(&fmt);
        QueueEvent(MakeEndFile(ClassifyAvError(ret)));
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

    // ── Discover sidecar files and build the audio/subtitle track list ────────
    ScanExternalSources(path);
    BuildTrackList(fmt, defaultAudioIdx);

    // ── Bind the default audio + subtitle selections ──────────────────────────
    AudioBinding aud;
    OpenAudioBinding(selectedAudioId_, fmt, aud);

    int subIdx = -1;
    AVCodecContext* sDec = nullptr;
    AVStream* sStream = nullptr;
    OpenSubtitleBinding(selectedSubId_, path, fmt, subIdx, sDec, sStream);

    if (!hasVideo_ && !aud.dec)
    {
        Log::Error("FFmpegPlayer: failed to open any decoder for {}", path);
        avcodec_free_context(&vDec);
        OpenAudioBinding(-1, fmt, aud); // tears down any partial audio binding
        OpenSubtitleBinding(-1, path, fmt, subIdx, sDec, sStream);
        avformat_close_input(&fmt);
        QueueEvent(MakeEndFile(EndFileReason::NoStream));
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

    // Fresh hit/miss counters for this file (the budget itself was configured via
    // SetReadAheadCache and persists across files).
    cache_.ResetMetrics();

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
                if (hasPendingSeek_)
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
                        return shutdown_.load() || hasPendingLoad_ || hasPendingSeek_;
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
        // advances on EndFile arrives as a new load (Stop); a manual seek resumes.
        {
            std::unique_lock lock(mutex_);
            cv_.wait(
                lock,
                [this]
                {
                    return shutdown_.load() || hasPendingLoad_ || hasPendingSeek_;
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
    {
        std::lock_guard lock(mutex_);
        advancing = hasPendingLoad_;
    }
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
        }

        // For audio-only files there is no video worker to drive TimePos.
        if (!hasVideo_)
        {
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
                               hasPendingSeek_;
                    }
                );
                if (shutdown_.load() || hasPendingLoad_ || hasPendingSeek_)
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
                        return shutdown_.load() || hasPendingLoad_ || hasPendingSeek_ || paused_.load();
                    }
                );
            }
            if (shutdown_.load() || hasPendingLoad_ || hasPendingSeek_)
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
    return shutdown_.load() || hasPendingLoad_;
}

double FFmpegPlayer::TakePendingSeek()
{
    std::lock_guard lock(mutex_);
    hasPendingSeek_ = false;
    return seekTarget_;
}

// ── Playback commands ─────────────────────────────────────────────────────────

void FFmpegPlayer::LoadFile(const char* path, double resumePos) noexcept
{
    {
        std::lock_guard lock(mutex_);
        pendingPath_ = path ? path : "";
        pendingResume_ = resumePos;
        hasPendingLoad_ = true;
    }
    // Wake the decode thread and unblock any workers waiting on a queue so the
    // current file is abandoned promptly.
    audioQ_->Abort();
    videoQ_->Abort();
    subQ_->Abort();
    Wake();
}

void FFmpegPlayer::SetPause(bool paused) noexcept
{
    paused_ = paused;
    audioOut_->SetPaused(paused);
    {
        std::lock_guard lock(mutex_);
        const auto now = std::chrono::steady_clock::now();
        if (paused)
        {
            pauseWall_ = now;
        }
        else if (videoClockSet_)
        {
            videoClockWall_ += now - pauseWall_; // shift baseline past the pause gap
        }
    }
    Wake();
    EmitFlag(PlayerProperty::Pause, paused);
    UpdateCoreIdle();
}

void FFmpegPlayer::TogglePause() noexcept
{
    SetPause(!paused_.load());
}

void FFmpegPlayer::ToggleMute() noexcept
{
    muteEnabled_ = !muteEnabled_;
    audioOut_->SetMute(muteEnabled_);
    EmitFlag(PlayerProperty::Mute, muteEnabled_);
}

void FFmpegPlayer::AdjustVolume(int delta) noexcept
{
    volume_ = std::clamp(volume_ + delta, 0, 100);
    audioOut_->SetVolume(volume_);
    EmitDouble(PlayerProperty::Volume, static_cast<double>(volume_));
}

void FFmpegPlayer::Seek(double seconds) noexcept
{
    if (idle_.load())
    {
        return; // nothing loaded — ignore (avoids seeking the next file opened)
    }
    // Accumulate relative seeks against a still-pending target rather than the master
    // clock: while a seek is in flight (e.g. a held arrow key auto-repeating) the clock
    // hasn't advanced to the previous target yet, so basing off it would make every
    // repeat re-target the same spot instead of stepping further each press.
    bool pending = false;
    double pendingTarget = 0.0;
    {
        std::lock_guard lock(mutex_);
        pending = hasPendingSeek_;
        pendingTarget = seekTarget_;
    }
    const double base = pending ? pendingTarget : GetMasterClock();
    RequestSeek(ClampSeekTarget(base + seconds, duration_.load()));
}

void FFmpegPlayer::SeekAbsolute(double seconds) noexcept
{
    if (idle_.load())
    {
        return;
    }
    RequestSeek(ClampSeekTarget(seconds, duration_.load()));
}

void FFmpegPlayer::RequestSeek(double target) noexcept
{
    {
        std::lock_guard lock(mutex_);
        seekTarget_ = target; // latest-wins: coalesces rapid seeks (seek-bar drags)
        hasPendingSeek_ = true;
    }
    // Perf timing: measure from the latest request to the first post-seek frame.
    FRAMELIFT_PERF_START("seek");
    // Unblock the demux read loop / keep-open wait and any worker mid-present.
    audioQ_->Abort();
    videoQ_->Abort();
    subQ_->Abort();
    Wake();
}

void FFmpegPlayer::SetImageDisplayDuration(double seconds) noexcept
{
    imageDisplayDuration_ = seconds; // <= 0 ⇒ hold a still image indefinitely
}

void FFmpegPlayer::SetAudioNormalize(bool enabled, const AudioNormalizeParams& params) noexcept
{
    {
        std::lock_guard lock(mutex_);
        normalizeParams_ = params;
    }
    normalizeEnabled_ = enabled;
    normalizeGen_.fetch_add(1); // the respawned worker rebuilds its graph from the flag
    EmitFlag(PlayerProperty::Normalize, enabled);

    if (idle_.load())
    {
        return; // no file playing — applied when the next file's audio worker starts
    }
    // Force a seek-to-current (like SelectAudioTrack) so the queued audio is flushed and
    // re-decoded from here with the new setting — otherwise the change isn't heard until
    // the device buffer drains (potentially several seconds).
    const double target = ClampSeekTarget(GetMasterClock(), duration_.load());
    {
        std::lock_guard lock(mutex_);
        seekTarget_ = target;
        hasPendingSeek_ = true;
    }
    audioQ_->Abort();
    videoQ_->Abort();
    subQ_->Abort();
    Wake();
}

void FFmpegPlayer::SetPlaybackOptions(const PlaybackOptions& opts) noexcept
{
    hrSeek_ = opts.hrSeek; // exact vs keyframe seeking
    hwdec_ = opts.hwdec;   // try hardware decode on the next load
    subAutoLoad_ = opts.subAutoLoad;
    audioFileAutoLoad_ = opts.audioFileAutoLoad;
}

void FFmpegPlayer::SetVideoDecodeMode(VideoDecodeMode mode) noexcept
{
    videoDecodeMode_ = mode;
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

void FFmpegPlayer::SetReadAheadCache(const ReadAheadCacheOptions& opts) noexcept
{
    // Takes effect immediately; the new byte budget governs the next WaitForSpace.
    cache_.Configure(opts.enabled, opts.maxBytes);
}

void FFmpegPlayer::SetSubtitleStyle(const SubtitleStyle& style) noexcept
{
    {
        std::lock_guard lock(tracksMutex_);
        subtitleStyle_ = style; // behavior fields read by BuildTrackList on the decode thread
    }
    // Styling is renderer-level libass state and persists across tracks; applying it
    // here makes the change visible on the next rendered frame without a seek.
    if (subtitles_)
    {
        subtitles_->ApplyStyle(style);
    }
    RequestRender();
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

void FFmpegPlayer::ApplySettings(const Settings& s)
{
    SetPlaybackOptions(ToPlaybackOptions(s.Get<PlaybackSettings>()));
    SetVideoDecodeMode(ToVideoDecodeMode(s.Get<PlaybackSettings>()));
    SetReadAheadCache(ToReadAheadCacheOptions(s.Get<CacheSettings>()));
    SetSubtitleStyle(ToSubtitleStyle(s.Get<SubtitleSettings>()));
    SetAudioPreferences(ToAudioPreferences(s.Get<AudioSettings>()));
    const AudioSettings& a = s.Get<AudioSettings>();
    SetAudioNormalize(a.normalizeEnabled, a.normalizeEnabled ? ToAudioNormalizeParams(a) : AudioNormalizeParams{});
}

void FFmpegPlayer::PulseDucking() noexcept
{
    audioOut_->PulseDuck();
}

void FFmpegPlayer::ToggleSubtitles() noexcept
{
    subtitlesEnabled_ = !subtitlesEnabled_;
    RequestRender(); // show/hide the overlay promptly, even while paused
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

void FFmpegPlayer::AdjustSubtitleDelay(double delta) noexcept
{
    subtitleDelay_ = subtitleDelay_.load() + delta;
    RequestRender();
}

void FFmpegPlayer::SetSubtitleDelay(double seconds) noexcept
{
    subtitleDelay_ = seconds;
    RequestRender();
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

// ── Async property queries ────────────────────────────────────────────────────

void FFmpegPlayer::GetDoubleAsync(PlayerProperty prop, void (*cb)(double, bool, void*), void* ud) noexcept
{
    if (!cb)
    {
        return;
    }
    const bool playing = !idle_.load();
    switch (prop)
    {
    case PlayerProperty::TimePos:
        cb(GetMasterClock(), playing, ud);
        return;
    case PlayerProperty::Duration: {
        const double d = duration_.load();
        cb(d, d > 0.0, ud);
        return;
    }
    case PlayerProperty::PercentPos: {
        const double d = duration_.load();
        cb(d > 0.0 ? GetMasterClock() / d * 100.0 : 0.0, playing && d > 0.0, ud);
        return;
    }
    case PlayerProperty::Volume:
        cb(static_cast<double>(volume_), true, ud);
        return;
    case PlayerProperty::Speed:
        cb(speed_, true, ud);
        return;
    default:
        cb(0.0, false, ud);
        return;
    }
}

void FFmpegPlayer::GetInt64Async(PlayerProperty prop, void (*cb)(int64_t, bool, void*), void* ud) noexcept
{
    if (!cb)
    {
        return;
    }
    switch (prop)
    {
    case PlayerProperty::DisplayWidth: {
        const int64_t v = displayWidth_.load();
        cb(v, v > 0, ud);
        return;
    }
    case PlayerProperty::DisplayHeight: {
        const int64_t v = displayHeight_.load();
        cb(v, v > 0, ud);
        return;
    }
    case PlayerProperty::DroppedFrames:
        cb(droppedFrames_.load(), true, ud);
        return;
    case PlayerProperty::MistimedFrames:
        cb(mistimedFrames_.load(), true, ud);
        return;
    case PlayerProperty::DecodeErrors:
        cb(decodeErrors_.load(), true, ud);
        return;
    case PlayerProperty::CacheUsed:
        cb(cache_.UsedKB(), true, ud);
        return;
    case PlayerProperty::CacheHits:
        cb(cache_.Hits(), true, ud);
        return;
    case PlayerProperty::CacheMisses:
        cb(cache_.Misses(), true, ud);
        return;
    default:
        cb(0, false, ud);
        return;
    }
}

void FFmpegPlayer::GetStringAsync(PlayerProperty prop, void (*cb)(const char*, bool, void*), void* ud) noexcept
{
    if (!cb)
    {
        return;
    }
    if (prop == PlayerProperty::Path)
    {
        std::string p;
        {
            std::lock_guard lock(mutex_);
            p = currentPath_;
        }
        cb(p.c_str(), !p.empty(), ud);
        return;
    }
    if (prop == PlayerProperty::MediaTitle)
    {
        std::string t;
        {
            std::lock_guard lock(mutex_);
            t = mediaTitle_;
        }
        cb(t.c_str(), !t.empty(), ud);
        return;
    }
    if (prop == PlayerProperty::HwDecCurrent)
    {
        std::string name;
        {
            std::lock_guard lock(mutex_);
            name = hwDecName_;
        }
        cb(name.c_str(), true, ud);
        return;
    }
    cb("", false, ud);
}

void FFmpegPlayer::GetDisplaySizeAsync(void (*cb)(const DisplaySize*, bool, void*), void* ud) noexcept
{
    if (!cb)
    {
        return;
    }
    const int64_t w = displayWidth_.load();
    const int64_t h = displayHeight_.load();
    if (w > 0 && h > 0)
    {
        const DisplaySize size{w, h};
        cb(&size, true, ud);
    }
    else
    {
        cb(nullptr, false, ud);
    }
}

void FFmpegPlayer::ObserveProperty(PlayerProperty prop) noexcept
{
    const auto idx = static_cast<std::size_t>(prop);
    if (idx >= kPropCount)
    {
        return;
    }
    observed_[idx] = true;

    // Emit the current value on subscription (so subscribers get an initial snapshot).
    switch (prop)
    {
    case PlayerProperty::IdleActive:
        EmitFlag(prop, idle_.load());
        break;
    case PlayerProperty::Pause:
        EmitFlag(prop, paused_.load());
        break;
    case PlayerProperty::Mute:
        EmitFlag(prop, muteEnabled_);
        break;
    case PlayerProperty::Normalize:
        EmitFlag(prop, normalizeEnabled_.load());
        break;
    case PlayerProperty::CoreIdle:
        EmitFlag(prop, coreIdle_.load());
        break;
    case PlayerProperty::EofReached:
        EmitFlag(prop, eofReached_.load());
        break;
    case PlayerProperty::Seeking:
        EmitFlag(prop, false); // not seeking at subscription time
        break;
    case PlayerProperty::PausedForCache:
        EmitFlag(prop, cache_.Stalling()); // true while a decode worker is stalled on a cache underrun
        break;
    case PlayerProperty::Duration:
        EmitDouble(prop, duration_.load());
        break;
    case PlayerProperty::Volume:
        EmitDouble(prop, static_cast<double>(volume_));
        break;
    case PlayerProperty::Speed:
        EmitDouble(prop, speed_);
        break;
    default:
        break; // TimePos / PercentPos stream from the workers
    }
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

// ── Rendering ─────────────────────────────────────────────────────────────────

void FFmpegPlayer::EnumerateAudioOutputDevices(void (*visit)(const AudioOutputDevice*, void*), void* ud) const noexcept
{
    audioOut_->EnumerateDevices(visit, ud);
}

void FFmpegPlayer::InitRender(void* graphicsBackend) noexcept
{
    auto* backend = static_cast<IGraphicsBackend*>(graphicsBackend);
    if (!backend)
    {
        return;
    }
    renderer_ = backend->CreateVideoRenderer();
    rendererReady_ = renderer_->Init(backend);
    if (!rendererReady_)
    {
        Log::Error("FFmpegPlayer: video renderer init failed; showing black");
    }

    // If the active backend is Vulkan and exposes a video-decode device, wrap it for
    // FFmpeg so we can decode straight onto the render device (#18). Non-fatal on
    // failure: vulkanZeroCopyAvailable_ stays false and PlayFile uses the readback /
    // CPU-RGBA8 paths.
#if FRAMELIFT_MODULE_GRAPHICS_VULKAN
    VulkanDeviceInfo vkInfo;
    if (backend->GetVulkanDeviceInfo(vkInfo) && vkInfo.supportsVideoDecode)
    {
        vkHwDevice_ = CreateVulkanHwDevice(vkInfo);
        vulkanZeroCopyAvailable_ = vkHwDevice_ != nullptr;
        vulkanAdapterIsNvidia_ = backend->HasNvidiaAdapter();
        if (vulkanZeroCopyAvailable_ && vulkanAdapterIsNvidia_)
        {
            Log::Debug("FFmpegPlayer: NVIDIA adapter — Auto decode prefers NVDEC over Vulkan zero-copy "
                      "(VK_ERROR_DEVICE_LOST on the NVIDIA Vulkan video backend)");
        }
    }
#endif
}

void FFmpegPlayer::ReleaseRender() noexcept
{
    rendererReady_ = false;
    renderer_.reset();
#if FRAMELIFT_MODULE_GRAPHICS_VULKAN
    vulkanZeroCopyAvailable_ = false;
    if (vkHwDevice_)
    {
        av_buffer_unref(&vkHwDevice_);
    }
#endif
}

void FFmpegPlayer::SetRenderUpdateCallback(void (*cb)(void*), void* ud) noexcept
{
    std::lock_guard lock(mutex_);
    renderCb_ = {cb, ud};
}

bool FFmpegPlayer::HasNewFrame() noexcept
{
    return newFramePending_.load();
}

void FFmpegPlayer::RenderFrame(int w, int h) noexcept
{
    PrepareRenderFrame(w, h);
    DrawPreparedFrame(w, h);
}

void FFmpegPlayer::PrepareRenderFrame(int w, int h) noexcept
{
    bool haveNew = false;
#if FRAMELIFT_MODULE_GRAPHICS_VULKAN
    bool haveNewVk = false;
#endif
    int dispW = 0;
    int dispH = 0;
    {
        std::lock_guard fl(frameMutex_);
        if (pendingValid_)
        {
#if FRAMELIFT_MODULE_GRAPHICS_VULKAN
            if (pendingIsVulkan_)
            {
                // Adopt the pending AVVkFrame; release the previously displayed one. The
                // timeline semaphore (signalled by the renderer's sample submit) keeps
                // FFmpeg from reusing the image until our GPU read completes, so dropping
                // our ref here is safe even if a submit is still in flight.
                if (displayVkFrame_)
                {
                    av_frame_free(&displayVkFrame_);
                }
                displayVkFrame_ = pendingVkFrame_;
                pendingVkFrame_ = nullptr;
                haveNewVk = true;
            }
            else
#endif
            {
                std::swap(displayPixels_, pendingPixels_);
                haveNew = true;
            }
            dispW = pendingW_;
            dispH = pendingH_;
            pendingValid_ = false;
        }
    }
    newFramePending_ = false;

    if (rendererReady_)
    {
#if FRAMELIFT_MODULE_GRAPHICS_VULKAN
        if (haveNewVk)
        {
            displayIsVulkan_ = true;
        }
        else if (haveNew)
        {
            displayIsVulkan_ = false;
            // Switched from the Vulkan path back to RGBA (e.g. a new software-decoded
            // file): drop the held AVVkFrame so its pool/device can be released.
            if (displayVkFrame_)
            {
                av_frame_free(&displayVkFrame_);
            }
        }

        if (displayIsVulkan_ && displayVkFrame_)
        {
            renderer_->UploadVulkanFrame(displayVkFrame_, dispW, dispH);
        }
        else if (haveNew)
#else
        if (haveNew)
#endif
        {
            renderer_->Upload(displayPixels_.data(), dispW, dispH);
        }

        // Render the libass subtitle overlay at the on-screen video size so it stays
        // crisp regardless of the source resolution, then composite it in Draw.
        preparedOverlayActive_ = false;
        const int videoW = static_cast<int>(displayWidth_.load());
        const int videoH = static_cast<int>(displayHeight_.load());
        if (subtitlesEnabled_ && subtitles_ && subtitles_->Ok() && videoW > 0 && videoH > 0)
        {
            const LetterboxRect vp = ComputeLetterbox(w, h, videoW, videoH);
            const auto timeMs =
                static_cast<long long>(std::llround((GetSubtitleRenderClock() - subtitleDelay_.load()) * 1000.0));
            const FFmpegSubtitles::RenderResult res =
                subtitles_->RenderOverlay(vp.w, vp.h, videoW, videoH, timeMs, overlayScratch_);
            if (res == FFmpegSubtitles::RenderResult::Updated)
            {
                renderer_->UploadOverlay(overlayScratch_.data(), vp.w, vp.h);
                preparedOverlayActive_ = true;
            }
            else if (res == FFmpegSubtitles::RenderResult::Unchanged)
            {
                preparedOverlayActive_ = true; // reuse the already-uploaded overlay texture
            }
        }
    }
}

void FFmpegPlayer::DrawPreparedFrame(int w, int h) noexcept
{
    if (rendererReady_)
    {
        renderer_->Draw(w, h, preparedOverlayActive_);
    }
}
