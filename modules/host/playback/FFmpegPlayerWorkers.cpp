#include "FFmpegPlayer.h"

#include "FFmpegPlayerInternal.h"

#include "FFmpegAudioFilter.h"
#include "FFmpegAudioOutput.h"
#include "FFmpegClock.h"
#include "FFmpegFilters.h"
#include "FFmpegHwDecode.h"
#include "FFmpegPacketQueue.h"
#include "SwsColorspace.h"
#include "VideoFrameDesc.h" // shared CPU-frame layout (graphics-core)

extern "C"
{
#include <libavutil/pixdesc.h> // av_pix_fmt_desc_get: RGB- vs YUV-family fallback split
}

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
        // Post-seek gate: hold the feed until the video worker paints the target
        // frame (seekSettled_), so an exact seek doesn't play audible audio over a
        // frozen picture while video is still decode-discarding toward the target.
        // Bounded and bail-aware — rationale and deadlock analysis at
        // ShouldReleaseAudioSeekHold (FFmpegClock.h). No-op in steady state and for
        // audio-only files (deliver itself settles those below).
        if (hasVideo_ && audioOut_->HasDevice())
        {
            const auto holdStart = std::chrono::steady_clock::now();
            std::unique_lock lock(mutex_);
            for (;;)
            {
                const bool superseded = seekKicked_;
                const bool tearingDown = shutdown_.load() || hasPendingLoad_ || stopRequested_;
                const double heldSec =
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - holdStart).count();
                if (ShouldReleaseAudioSeekHold(seekSettled_, superseded, tearingDown, heldSec, kSeekAudioHoldLimit))
                {
                    if (superseded || tearingDown)
                    {
                        return; // stale audio for a superseded seek / teardown — drop it
                    }
                    break;
                }
                cv_.wait_for(lock, std::chrono::milliseconds(50));
            }
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
            if (seekRefresh_.load())
            {
                // Same rule as the video worker's present(): "seek" ends only on the
                // first post-seek delivery, and audio-only must clear the refresh flag
                // itself (no video worker exists to do it).
                FRAMELIFT_PERF_END("seek");
            }
            seekRefresh_ = false;

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

        // Pace this worker to the device's real-time drain. The PCM ring the
        // device pulls from is unbounded, so without a brake here the worker
        // decodes the entire read-ahead into raw PCM at once and then waits on an
        // empty packet queue — inflating memory (seconds of F32 PCM instead of
        // compressed packets) and bracketing a phantom "cache-stall" whenever the
        // demuxer is parked on the video queue's backpressure. Holding the unheard
        // backlog near 0.5 s keeps audio compressed in the packet queue (where the
        // read-ahead budget governs it) and makes an empty audio queue mean a real
        // underrun again. Every control change (pause/seek/load/stop) Wake()s cv_,
        // so this wait never outlives one.
        constexpr double kMaxQueuedAudioSec = 0.5;
        for (;;)
        {
            const int bps = audioOut_->BytesPerSec();
            if (!audioOut_->HasDevice() || bps <= 0 || audioQ_->Aborted() || audioQ_->AtEof())
            {
                // No drain to pace against, a seek/stop tore the queue down, or the
                // session hit EOF — at EOF the worker must drain and exit promptly
                // (the join in RunDemuxSession is waiting), matching the pre-pacing
                // behavior: the tail rides the ring while the video worker paces out.
                break;
            }
            const double queuedSec = static_cast<double>(audioOut_->QueuedBytes()) / bps;
            if (queuedSec <= kMaxQueuedAudioSec)
            {
                break;
            }
            std::unique_lock lock(mutex_);
            if (shutdown_.load() || hasPendingLoad_ || stopRequested_ || hasPendingSeek_)
            {
                break; // let the Pop loop observe the abort/EOF promptly
            }
            if (paused_.load())
            {
                // The sink is suspended, so the backlog can't drain — park, but with
                // a timeout: session teardown at EOF signals the queue (SignalEof),
                // which neither wakes cv_ nor appears in this predicate, so an
                // indefinite wait here could hang the worker join.
                cv_.wait_for(
                    lock, std::chrono::milliseconds(500),
                    [this]
                    {
                        return !paused_.load() || shutdown_.load() || hasPendingLoad_ || hasPendingSeek_ ||
                               stopRequested_;
                    }
                );
                continue;
            }
            const double excess = queuedSec - kMaxQueuedAudioSec;
            const auto slice = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(std::clamp(excess, 0.01, 0.1))
            );
            cv_.wait_for(
                lock, slice,
                [this]
                {
                    return shutdown_.load() || hasPendingLoad_ || hasPendingSeek_ || stopRequested_ || paused_.load();
                }
            );
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
    // Track what sws_setColorspaceDetails() was last applied to so we only reconfigure
    // the YUV→RGB matrix / input range when the context is (re)created or the source
    // frame's colorspace/range actually changes (both are stable across a stream).
    SwsContext* swsConfigured = nullptr;
    int swsCoeff = -1;
    int swsSrcRange = -1;
    std::vector<uint8_t> pixelBuf; // reused CPU frame buffer (planar YUV or RGBA; sized at publish)
    bool sentReconfig = false;
    bool clockStallWarned = false; // one warning per stall episode (reset on a normal present)

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
        // The pts is read off the decoded frame directly — for hardware frames it is
        // identical on the downloaded copy (MapToSoftware copies the frame props) — so
        // the exact-seek discard runs BEFORE any GPU readback: while decode-discarding
        // toward an exact-seek target, a discarded hw frame must not pay a full
        // av_hwframe_transfer_data just to be thrown away.
        const double framePts = FramePtsSec(decoded, tb);
        if (framePts < seekSkipPts_) // exact-seek: discard frames before the target
        {
            return false;
        }

        AVFrame* f = decoded;
        if (!vkFrame && hw && hw->Active() && decoded->format == hw->HwPixelFormat())
        {
            f = hw->MapToSoftware(decoded, swFrame);
            if (!f)
            {
                return false; // transfer hiccup: skip this frame, no clock side-effects
            }
        }

        // Emit VideoReconfig on the first frame and whenever the decoded size
        // changes; scale to the native size (the renderer letterboxes on draw).
        if (!sentReconfig || f->width != dstW || f->height != dstH)
        {
            dstW = f->width;
            dstH = f->height;
            displayWidth_ = dstW;
            displayHeight_ = dstH;
            sentReconfig = true;
            QueueEvent(MakeLifecycle(MediaEventType::VideoReconfig));
        }

        // Establish the video-only wall-clock baseline on the first frame.
        if (!audioOut_->HasDevice())
        {
            std::lock_guard lock(mutex_);
            if (videoClock_.EstablishOnce(framePts, std::chrono::steady_clock::now()))
            {
                subtitleSeekClockOverrideActive_ = false;
                seekClockValid_ = true; // video wall clock is the master here — anchor may release
            }
        }

        // Baseline for the clock-stall watchdog below: how long this frame has been
        // held and how far the master clock moved in that time.
        const auto holdStart = std::chrono::steady_clock::now();
        const double masterAtHold = GetMasterClock();
        bool heldByWatchdog = false;

        // A post-seek refresh frame paints immediately, skipping the pacing loop:
        // the master clock is not re-established yet (the seek flushed the audio
        // pipeline; the clock reads ~0 and advances only as freshly fed audio
        // drains), so pacing this frame against it would gate the seek's visible
        // feedback on audio flowing again — and holding here parks the demuxer
        // via queue backpressure, which can deadlock the whole pipeline when the
        // hold itself is what blocks audio delivery. Audio re-anchors the clock
        // and the *next* frames pace normally.
        const bool paceThisFrame = !seekRefresh_.load();

        while (paceThisFrame)
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
                // so the target frame is shown before the loop bails to re-seek. A
                // *kicked* seek (seekKicked_) bails immediately: this frame predates
                // the request, and pacing it out would stall the seek by up to a
                // frame interval.
                if (shutdown_.load() || hasPendingLoad_ || stopRequested_ || (hasPendingSeek_ && seekSettled_) ||
                    seekKicked_)
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

            // Liveness: never wait indefinitely on a clock that is not advancing
            // (audio starved or the sink stalled). Holding this frame also parks
            // the demuxer via queue backpressure — video consumption is what
            // un-parks the pipeline and restores audio delivery — so present it
            // and let the clock re-anchor when audio recovers.
            const double heldSec = std::chrono::duration<double>(std::chrono::steady_clock::now() - holdStart).count();
            if (!paused_.load() && ShouldBreakFrameHold(heldSec, master - masterAtHold, kFrameHoldLimit))
            {
                mistimedFrames_.fetch_add(1);
                if (!clockStallWarned)
                {
                    clockStallWarned = true;
                    Log::Warn(
                        "FFmpegPlayer: master clock stalled (held frame {}s at clock {}); presenting to "
                        "keep the pipeline alive",
                        heldSec, master
                    );
                }
                heldByWatchdog = true;
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
            if (shutdown_.load() || hasPendingLoad_ || stopRequested_ || (hasPendingSeek_ && seekSettled_) ||
                seekKicked_)
            {
                return true;
            }
        }

        if (!heldByWatchdog)
        {
            clockStallWarned = false; // a frame paced normally — the stall episode is over
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
            const auto srcPixFmt = static_cast<AVPixelFormat>(f->format);
            VideoFrameDesc desc;
            desc.w = dstW;
            desc.h = dstH;
            desc.colorspace = f->colorspace;
            desc.fullRange = SwsColorspace::FullRange(f->color_range);

            if (srcPixFmt == AV_PIX_FMT_NV12 || srcPixFmt == AV_PIX_FMT_YUV420P)
            {
                // Fast path — software 8-bit decode (I420) and every hw readback (NV12)
                // land here: copy the planes tightly, no swscale at all. The renderer
                // does the YUV→RGB matrix on the GPU (the shader gets desc's
                // colorspace/range), which both removes the per-frame CPU conversion
                // and shrinks the upload from 4 to 1.5 bytes per pixel.
                desc.format = srcPixFmt == AV_PIX_FMT_NV12 ? VideoPixelFormat::NV12 : VideoPixelFormat::YUV420P;
                const size_t bytes = FillTightLayout(desc);
                if (pixelBuf.size() != bytes)
                {
                    pixelBuf.resize(bytes);
                }
                for (int p = 0; p < PlaneCount(desc.format); ++p)
                {
                    const int rowBytes = desc.stride[p];
                    const int rows = PlaneRows(desc.format, p, desc.h);
                    const uint8_t* src = f->data[p];
                    uint8_t* dst = pixelBuf.data() + desc.planeOffset[p];
                    if (f->linesize[p] == rowBytes)
                    {
                        std::memcpy(dst, src, static_cast<size_t>(rowBytes) * rows);
                    }
                    else
                    {
                        for (int r = 0; r < rows; ++r)
                        {
                            std::memcpy(
                                dst + static_cast<size_t>(r) * rowBytes,
                                src + static_cast<ptrdiff_t>(r) * f->linesize[p], rowBytes
                            );
                        }
                    }
                }
            }
            else
            {
                // Fallback for everything else (10-bit, 4:2:2/4:4:4, P010 readback, RGB
                // sources): swscale once per frame. YUV-family sources resample to NV12 —
                // depth/chroma only, no color matrix, so the tag rides through desc to
                // the shader like the fast path. RGB-family sources convert to RGBA
                // (there is no YUV matrix to apply; the shader samples them verbatim).
                const AVPixFmtDescriptor* fmtDesc = av_pix_fmt_desc_get(srcPixFmt);
                const bool rgbSource = fmtDesc && (fmtDesc->flags & AV_PIX_FMT_FLAG_RGB) != 0;
                desc.format = rgbSource ? VideoPixelFormat::RGBA : VideoPixelFormat::NV12;
                const size_t bytes = FillTightLayout(desc);

                sws = sws_getCachedContext(
                    sws, f->width, f->height, srcPixFmt, dstW, dstH, rgbSource ? AV_PIX_FMT_RGBA : AV_PIX_FMT_NV12,
                    SWS_BILINEAR, nullptr, nullptr, nullptr
                );
                if (!sws)
                {
                    return false;
                }
                if (rgbSource)
                {
                    // RGBA output is already display-referred; make sure a previously
                    // configured YUV matrix doesn't linger on the cached context.
                    const int coeff = SwsColorspace::ResolveCoefficients(f->colorspace, f->height);
                    const int srcRange = SwsColorspace::FullRange(f->color_range);
                    if (sws != swsConfigured || coeff != swsCoeff || srcRange != swsSrcRange)
                    {
                        sws_setColorspaceDetails(
                            sws, sws_getCoefficients(coeff), srcRange, sws_getCoefficients(SWS_CS_DEFAULT),
                            /*dstRange=*/1, /*brightness=*/0, /*contrast=*/1 << 16, /*saturation=*/1 << 16
                        );
                        swsConfigured = sws;
                        swsCoeff = coeff;
                        swsSrcRange = srcRange;
                    }
                }
                if (pixelBuf.size() != bytes)
                {
                    pixelBuf.resize(bytes);
                }
                uint8_t* dst[4] = {
                    pixelBuf.data() + desc.planeOffset[0],
                    desc.format == VideoPixelFormat::NV12 ? pixelBuf.data() + desc.planeOffset[1] : nullptr, nullptr,
                    nullptr
                };
                int dstStride[4] = {desc.stride[0], desc.stride[1], 0, 0};
                sws_scale(sws, f->data, f->linesize, 0, f->height, dst, dstStride);
            }

            frameGate_.PublishPixels(pixelBuf, desc); // swap: worker reuses the returned buffer
        }
        RequestRender();

        // Perf timing: the first presented frame ends whichever op is in flight.
        // Each END is a no-op until its matching START, so calling it every frame
        // is safe; a resume-position seek on load never STARTs "seek", so it stays
        // folded into "file-open". "seek" may only end on a post-seek frame
        // (seekRefresh_): a stale pre-seek frame this worker was already pacing when
        // the seek kicked would otherwise consume the timer and the real target
        // frame would log nothing.
        FRAMELIFT_PERF_END("file-open");
        if (seekRefresh_.load())
        {
            FRAMELIFT_PERF_END("seek");
        }

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
        cv_.notify_all(); // release the audio worker's post-seek hold promptly
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

    // hr-seek framedrop margin: frames within ~2 intervals of the target always
    // decode fully, so timestamp jitter can't skip a frame the target references.
    const double discardMargin = frameInterval > 0.0 ? 2.0 * frameInterval : 0.2;

    bool interrupted = false;
    while (!interrupted && videoQ_->Pop(pkt))
    {
        // While decoding toward an exact-seek target, let the decoder skip
        // non-reference frames that the present path would discard anyway —
        // references still decode, so the target frame is unaffected. skip_frame
        // may change between send_packet calls; the decoder is worker-owned.
        const int64_t rawTs = pkt->pts != AV_NOPTS_VALUE ? pkt->pts : pkt->dts;
        const double pktSec = rawTs == AV_NOPTS_VALUE ? std::numeric_limits<double>::quiet_NaN()
                                                      : static_cast<double>(rawTs) * av_q2d(tb);
        dec->skip_frame = DecideSeekDiscard(pktSec, seekSkipPts_, discardMargin) == SeekDiscardMode::SkipNonRef
                              ? AVDISCARD_NONREF
                              : AVDISCARD_DEFAULT;
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
        dec->skip_frame = AVDISCARD_DEFAULT; // never drain the tail in skip mode
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
