#include "FFmpegPlayer.h"

#include "FFmpegPlayerInternal.h"

#include "FFmpegAudioOutput.h"
#include "FFmpegClock.h"
#include "FFmpegPacketQueue.h"

#include <chrono>
#include <exception>
#include <format>
#include <mutex>
#include <string>

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
    // The gate's opaque channel carries ref'd AVFrames (Vulkan zero-copy); teach it
    // how to drop one so its header stays libav-free.
    frameGate_.SetOpaqueRelease(
        [](void* p)
        {
            auto* f = static_cast<AVFrame*>(p);
            av_frame_free(&f);
        }
    );
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
    frameGate_.ReleaseAll();
#if FRAMELIFT_MODULE_GRAPHICS_VULKAN
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
    eventSink_.Queue(e);
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
    eventSink_.EmitFlag(prop, value);
}

void FFmpegPlayer::EmitDouble(PlayerProperty prop, double value)
{
    eventSink_.EmitDouble(prop, value);
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

// ── Events ────────────────────────────────────────────────────────────────────

MediaEvent FFmpegPlayer::PollEvent() noexcept
{
    return eventSink_.Poll(); // type == MediaEventType::None when empty
}

void FFmpegPlayer::SetWakeupCallback(void (*cb)(void*), void* ud) noexcept
{
    eventSink_.SetWakeupCallback(cb, ud);
}

