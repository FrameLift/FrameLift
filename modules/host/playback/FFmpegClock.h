#pragma once

#include <cstdint>

// Pure A/V-sync math for the FFmpeg backend (issue #8, Phase 3).
//
// Deliberately free of any libav / SDL include so it can be unit-tested with the
// native compiler (tests/FFmpegClockTests.cpp) — the standalone CI test build has
// neither FFmpeg nor SDL available. Everything here operates on plain seconds /
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
