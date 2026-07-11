#pragma once

#include <chrono>
#include <cstdint>

// Pure A/V-sync math for the FFmpeg backend (issue #8, Phase 3).
//
// Deliberately free of any libav / platform-audio include so it can be unit-tested
// with the native compiler (tests/FFmpegClockTests.cpp) — the standalone CI test
// build has neither FFmpeg nor Qt Multimedia available. Everything here operates on plain seconds /
// bytes and is therefore trivially exercised in isolation.

// The audio clock is the master clock. We know the presentation timestamp of the
// last audio sample handed to the device (lastQueuedPts) and how many bytes are
// still sitting in the device's queue, not yet audible. Subtracting the duration
// of that backlog yields the timestamp of the audio currently being heard.
//
//   masterClock = lastQueuedPts - queuedBytes / bytesPerSec
//
// queuedBytes naturally stops draining while the device is paused, which freezes
// the returned clock for free. bytesPerSec <= 0 (device not open yet) falls back
// to lastQueuedPts so callers never divide by zero.
inline double ComputeMasterClock(double lastQueuedPts, int64_t queuedBytes, int bytesPerSec)
{
    if (bytesPerSec <= 0)
    {
        return lastQueuedPts;
    }
    return lastQueuedPts - static_cast<double>(queuedBytes) / static_cast<double>(bytesPerSec);
}

// Subtitle rendering normally follows the master clock, but right after a seek the
// audio/video clocks can briefly be stale or reset until the first post-seek sample
// is presented. During that window, render subtitles at the landed seek target.
inline double SelectSubtitleRenderClock(double masterClock, bool seekOverrideActive, double seekTarget)
{
    return seekOverrideActive ? seekTarget : masterClock;
}

// What the video worker should do with a decoded frame, given where the master
// clock currently sits.
enum class FrameAction : std::uint8_t
{
    Present, // frame is roughly due now — show it
    Drop,    // frame is more than dropThreshold seconds late — skip it to catch up
    Wait,    // frame is in the future — sleep until its time, then show it
};

// Decide how to handle a frame whose presentation timestamp is framePts while the
// master clock reads masterClock. dropThreshold is how far behind a frame may fall
// before it is discarded rather than displayed (typ. ~0.1s).
//
//   diff > 0          → frame is early              → Wait
//   -threshold <= diff → frame is on time / slightly late → Present
//   diff < -threshold → frame is too late          → Drop
inline FrameAction DecideFrame(double framePts, double masterClock, double dropThreshold)
{
    const double diff = framePts - masterClock;
    if (diff > 0.0)
    {
        return FrameAction::Wait;
    }
    if (diff < -dropThreshold)
    {
        return FrameAction::Drop;
    }
    return FrameAction::Present;
}

// A presented frame counts as "mistimed" only when it lags the master clock by more
// than `tolerance` seconds. A frame always wakes a hair late (never early — that would
// re-Wait), so sub-frame pacing jitter must not be counted or smooth playback reads
// ~100% mistimed. Callers pass half the frame interval as the tolerance, so only
// frames that slip a genuine fraction of a frame behind are flagged.
inline bool IsMistimedFrame(double framePts, double masterClock, double tolerance)
{
    return (masterClock - framePts) > tolerance;
}

// Video-only wall-clock fallback: when a file has no audio device, the master
// clock is derived from the first presented frame's pts and the wall clock, and
// must freeze across pause. Deliberately NOT internally locked — every member
// function must be called under FFmpegPlayer::mutex_, because establishment and
// pause edges share compound critical sections with the seek gates there.
struct VideoWallClockState
{
    double pts = 0.0;
    bool set = false;
    std::chrono::steady_clock::time_point wall;      // wall time `pts` maps to
    std::chrono::steady_clock::time_point pauseWall; // wall time playback paused at

    // Anchor the baseline on the first presented frame; later frames are no-ops.
    // Returns true when this call established it (the caller re-arms seek gates).
    bool EstablishOnce(double framePts, std::chrono::steady_clock::time_point now)
    {
        if (set)
        {
            return false;
        }
        set = true;
        pts = framePts;
        wall = now;
        pauseWall = now;
        return true;
    }

    // Pause edge: remember when playback froze; on resume, shift the baseline
    // past the pause gap so the clock continues from where it stopped.
    void OnPauseEdge(bool paused, std::chrono::steady_clock::time_point now)
    {
        if (paused)
        {
            pauseWall = now;
        }
        else if (set)
        {
            wall += now - pauseWall;
        }
    }

    // Current clock reading (seconds); 0 until established. While paused, reads
    // as of the pause instant so the clock is frozen.
    [[nodiscard]] double Read(bool paused, std::chrono::steady_clock::time_point now) const
    {
        if (!set)
        {
            return 0.0;
        }
        const auto ref = paused ? pauseWall : now;
        return pts + std::chrono::duration<double>(ref - wall).count();
    }

    // Invalidate (new file / applied seek); the next presented frame re-anchors.
    void Reset()
    {
        set = false;
    }
};

// Liveness guard for the frame-pacing wait. A frame may wait on the master clock
// only while that clock is actually advancing: when audio is starved or the sink
// stalls (post-seek interleave gap, device trouble), the clock pins and the frame
// would wait forever — and holding it also parks the demuxer via queue
// backpressure, which is what turns an audio hiccup into a permanent freeze
// (video consumption is what un-parks the pipeline, which is what restores
// audio).
//
// Track the most recent meaningful forward progress rather than total movement
// since the frame began waiting. This catches a clock that advances and then
// stalls, while Reset() lets callers exclude time spent deliberately paused. The
// 1 ms threshold is cumulative from the last reset/progress point, so harmless
// sub-millisecond sampling jitter cannot keep a pinned clock alive indefinitely.
class FrameHoldWatchdog
{
public:
    using TimePoint = std::chrono::steady_clock::time_point;

    FrameHoldWatchdog(double masterClock, TimePoint now) noexcept
    {
        Reset(masterClock, now);
    }

    void Reset(double masterClock, TimePoint now) noexcept
    {
        progressClock_ = masterClock;
        stalledSince_ = now;
    }

    [[nodiscard]] bool ShouldBreak(double masterClock, TimePoint now, double holdLimitSec) noexcept
    {
        if (masterClock - progressClock_ >= kMeaningfulProgressSec)
        {
            Reset(masterClock, now);
            return false;
        }
        return StalledFor(now) >= holdLimitSec;
    }

    [[nodiscard]] double StalledFor(TimePoint now) const noexcept
    {
        return std::chrono::duration<double>(now - stalledSince_).count();
    }

private:
    static constexpr double kMeaningfulProgressSec = 1e-3;

    double progressClock_ = 0.0;
    TimePoint stalledSince_{};
};

// Post-seek audio hold. With video present, the audio worker holds its first
// post-seek feeds until the video worker paints the target frame — otherwise an
// exact seek plays audible audio (advancing the master clock) over a frozen
// picture while video is still decode-discarding toward the target, and video
// then has to drop frames to re-sync. Release when:
//   • videoSettled  — the post-seek frame painted (normal path; the hold is done),
//   • seekSuperseded — a newer kicked seek made this audio stale (caller drops it),
//   • tearingDown   — load/stop/shutdown (caller drops it),
//   • timeout       — video can't paint (decode errors, video stream ends before
//     audio at the seek point): bounded silence, then audio resumes anyway — the
//     same self-healing philosophy as FrameHoldWatchdog.
// Deadlock-free by construction: the video worker's post-seek refresh frame
// presents UNPACED (it never waits on the audio clock this hold freezes), and the
// demuxer park this hold can cause is bounded by the audio queue's packet cap
// plus this timeout.
inline bool ShouldReleaseAudioSeekHold(
    bool videoSettled, bool seekSuperseded, bool tearingDown, double heldSec, double holdLimitSec
)
{
    return videoSettled || seekSuperseded || tearingDown || heldSec >= holdLimitSec;
}

// Exact (hr) seeks decode forward from the landed keyframe and discard every frame
// before the target — pure overhead. Non-reference frames in that window can be
// skipped by the decoder entirely (AVDISCARD_NONREF) without affecting the target:
// reference frames still decode, so the presented frame is bit-exact. The decision
// is safe purely per-packet: any frame whose pts is inside the skip window would be
// discarded by the present path regardless, so skipping its decode can never change
// what is shown — even for late reordered packets after the target has presented.
enum class SeekDiscardMode : std::uint8_t
{
    DecodeAll,  // no live exact-seek target, or the frame is near/after it
    SkipNonRef, // decoder may skip non-reference frames (discard window)
};

// pktPtsSec: the packet's pts (fallback dts) in seconds; NaN when unknown — unknown
// timestamps must decode normally (NaN compares false, yielding DecodeAll).
// seekSkipPts: the live exact-seek target, or a huge negative sentinel (< -1e17)
// for keyframe seeks / normal playback. margin keeps frames near the target decoded
// even when packet timestamps jitter (B-frame reorder): callers pass ~2 frame
// intervals.
inline SeekDiscardMode DecideSeekDiscard(double pktPtsSec, double seekSkipPts, double margin)
{
    if (seekSkipPts < -1e17)
    {
        return SeekDiscardMode::DecodeAll;
    }
    return pktPtsSec < seekSkipPts - margin ? SeekDiscardMode::SkipNonRef : SeekDiscardMode::DecodeAll;
}

// Clamp a requested seek target (seconds) to the playable range. Negative targets
// (seeking before the start) clamp to 0; targets past the end clamp to duration.
// A non-positive duration means the length is unknown — only the lower bound applies.
inline double ClampSeekTarget(double target, double duration)
{
    if (target < 0.0)
    {
        return 0.0;
    }
    if (duration > 0.0 && target > duration)
    {
        return duration;
    }
    return target;
}
