#pragma once

// Internal to the FFmpegPlayer*.cpp translation units in this directory ONLY.
// It pulls in <libav*/...> headers, so it must never be included from
// FFmpegPlayer.h or anything outside modules/host/playback (App.cpp includes
// FFmpegPlayer.h and has to stay libav-free).

#include "LogRepeatCollapse.h"

#include <framelift/Log.h>
#include <framelift/platform/IMediaPlayer.h>

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

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX // keep std::min/std::max usable in the including TU
#endif
// clang-format off
#include <windows.h>
#include <timeapi.h> // timeBeginPeriod / timeEndPeriod (winmm); needs windows.h types first
// clang-format on
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002 // Win10 1803+; define for older SDK headers
#endif
#endif

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>

// ── Perf-pair contract ────────────────────────────────────────────────────────
// These FRAMELIFT_PERF_START/END names are string-matched pairs whose START and
// END live in DIFFERENT translation units (and different threads). Renaming one
// side silently breaks the measurement — keep them in sync:
//   "file-open"          START PlayFile (Session)            END AudioWorker /
//                        VideoWorker first present (Workers), PlayFile error
//                        paths (Session), DecodeThreadMain catch (core)
//   "file-load-metadata" START PlayFile (Session)            END same set as above
// Seek timings use a :g<generation> suffix. Request-to-first-frame timing is kept
// by FFmpegPlayer so superseded generations leave no orphaned name-keyed timer;
// decoder Flush results carry their request timestamp to the owning worker, measuring
// acknowledgement latency rather than only avcodec_flush_buffers() call time.
// EmitPlaybackSummary() must remain reachable from every session exit path.

namespace ffplay_detail
{

inline std::string SeekPerfName(const char* phase, std::uint64_t generation)
{
    return std::string(phase) + ":g" + std::to_string(generation);
}

inline void LogSeekPerf(const char* phase, std::uint64_t generation, std::chrono::steady_clock::time_point started)
{
    if (!Log::PerfActive())
    {
        return;
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    const std::string name = SeekPerfName(phase, generation);
    Log::Perf(name.c_str(), ms);
}

// RAII wrapper for a self-contained FRAMELIFT_PERF_START/END span.
class PerfScope
{
public:
    explicit PerfScope(std::string name) : name_(std::move(name))
    {
        FRAMELIFT_PERF_START(name_.c_str());
    }

    ~PerfScope()
    {
        FRAMELIFT_PERF_END(name_.c_str());
    }

    PerfScope(const PerfScope&) = delete;
    PerfScope& operator=(const PerfScope&) = delete;

private:
    std::string name_;
};

inline MediaEvent MakeLifecycle(MediaEventType type)
{
    MediaEvent e;
    e.type = type;
    return e;
}

inline MediaEvent MakeEndFile(EndFileReason reason)
{
    MediaEvent e;
    e.type = MediaEventType::EndFile;
    e.endReason = reason;
    return e;
}

// A non-fatal notice (playback continues): the kind rides in property.value.i64, which
// PollEvent copies by value like any other field. The host maps it to a notification.
inline MediaEvent MakeNotice(MediaNoticeKind kind)
{
    MediaEvent e;
    e.type = MediaEventType::Notice;
    e.property.type = PropertyType::Int64;
    e.property.value.i64 = static_cast<int64_t>(kind);
    return e;
}

// A packet failed to decode: bump the counter and warn, but throttle the log so a
// badly corrupt stream can't flood it (first error, then every 100th).
inline void CountDecodeError(std::atomic<int64_t>& counter, const AVCodecContext* dec)
{
    const int64_t n = counter.fetch_add(1) + 1;
    if (n == 1 || n % 100 == 0)
    {
        Log::Warn("FFmpegPlayer: {} decode error (total {})", av_get_media_type_string(dec->codec_type), n);
    }
}

// Error/warning lines libav emits on this thread that are *expected* and should be
// demoted to Debug instead of alarming the log. The audio worker arms this around
// avcodec_send_packet for the first packets after a codec flush: in non-interleaved
// AVIs every seek hands the mp3 decoder 1-3 misaligned leading packets, and the
// decoder logs "Header missing" per packet before the parser resyncs — inherent
// demuxer behavior (plain ffmpeg CLI logs the same), not a playback fault. Audio
// decode is synchronous on the calling thread, so thread_local scoping catches
// exactly the armed send's lines and nothing else.
inline thread_local int gExpectedResyncErrorLogs = 0;

// Route FFmpeg's libav* logging through the host logger (mirrors AssLogCallback for
// libass). FFmpeg's default callback writes raw to stderr, bypassing our message
// pattern *and* the IsSuppressed filter in Log.cpp; routing it through Log::* gives
// it timestamps/colors, lands it in the Log Viewer buffer, and lets IsSuppressed drop
// known-benign container noise (UDTA/chapter/codec-params) from stderr while keeping it
// in the buffer. Only messages at or below the active av_log level reach us (default
// AV_LOG_INFO), so verbose/debug spam never arrives unless explicitly enabled.
inline void FFmpegLogCallback(void* /*avcl*/, int level, const char* fmt, va_list va)
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

    if (gExpectedResyncErrorLogs > 0 && level <= AV_LOG_WARNING)
    {
        --gExpectedResyncErrorLogs;
        Log::Debug("FFmpeg: {} (post-seek resync)", buf);
        return;
    }

    // Collapse repeat storms (mp3 "Header missing" once per misaligned packet after
    // each seek in an AVI, and similar per-packet libav errors) into "(xN)" lines.
    // The callback arrives on any decode/demux thread, so guard the shared collapser.
    static std::mutex repeatMutex;
    static LogRepeatCollapser collapser(std::chrono::seconds(3));

    std::lock_guard lock(repeatMutex);
    collapser.Observe(
        level, buf, std::chrono::steady_clock::now(),
        [](int lvl, const char* msg, std::uint64_t occurrences)
        {
            char line[1064];
            if (occurrences > 1)
            {
                std::snprintf(line, sizeof(line), "%s (x%llu)", msg, static_cast<unsigned long long>(occurrences));
            }
            else
            {
                std::snprintf(line, sizeof(line), "%s", msg);
            }
            if (lvl <= AV_LOG_ERROR)
            {
                Log::Error("FFmpeg: {}", line);
            }
            else if (lvl <= AV_LOG_WARNING)
            {
                Log::Warn("FFmpeg: {}", line);
            }
            else
            {
                Log::Info("FFmpeg: {}", line);
            }
        }
    );
}

// (Re)install our av_log callback. av_log_set_callback is global libav state, and on
// Linux the Qt Multimedia FFmpeg backend (loaded lazily via QMediaDevices/QAudioSink)
// shares the same libav* and clobbers it with its own handler — so a one-shot install
// in the constructor doesn't stick. Re-assert it right before each open instead; the
// call is cheap (just a function-pointer store) and idempotent.
inline void InstallFFmpegLogCallback()
{
    av_log_set_callback(&FFmpegLogCallback);
}

// Presentation timestamp of a decoded frame in seconds (0 when unknown).
inline double FramePtsSec(const AVFrame* f, AVRational tb)
{
    int64_t pts = f->best_effort_timestamp;
    if (pts == AV_NOPTS_VALUE)
    {
        pts = f->pts;
    }
    return pts == AV_NOPTS_VALUE ? 0.0 : static_cast<double>(pts) * av_q2d(tb);
}

} // namespace ffplay_detail
