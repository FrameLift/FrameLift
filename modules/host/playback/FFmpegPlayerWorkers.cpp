#include "FFmpegPlayer.h"

#include "FFmpegPlayerInternal.h"

#include "FFmpegAudioFilter.h"
#include "FFmpegAudioOutput.h"
#include "FFmpegClock.h"
#include "FFmpegFilters.h"
#include "FFmpegHwDecode.h"
#include "FFmpegPacketQueue.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

using namespace ffplay_detail;

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
                frameGate_.PublishOpaque(clone, dstW, dstH);
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

            frameGate_.PublishPixels(rgba, dstW, dstH); // swap: worker reuses the returned buffer
        }
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
