#pragma once

#include <cstdint>
#include <cstring>

// Seek precision policy — pure logic (FFmpegClock.h precedent: header-only,
// libav-free, unit-tested in the standalone suite).
//
// `playback.seekMode` selects when seeks decode precisely to the requested time
// versus snapping to a container keyframe, mirroring mpv's `hr-seek`:
//   smart    — relative seeks (arrow keys) snap to keyframes for speed; absolute
//              seeks (seek bar, resume, track switches) are exact. Audio-only
//              files seek exactly everywhere (decode-to-target is nearly free
//              and keyframe landing would be needlessly imprecise). This is
//              mpv's `hr-seek=default`, the old-player behavior — the default.
//   exact    — every seek decodes to the requested time (`hr-seek=yes`).
//   keyframe — every seek snaps to a keyframe (`hr-seek=no`).
enum class SeekPrecisionMode : uint8_t
{
    Smart,
    Exact,
    Keyframe,
};

// How one particular seek request is to be executed. Forward keyframe seeks land
// on the first keyframe AT OR AFTER the target (no AVSEEK_FLAG_BACKWARD), so a
// +5s step always advances even when the GOP is longer than the step — mpv's
// SEEK_FORWARD behavior. Backward/absolute keyframe seeks land at or before.
enum class SeekKind : uint8_t
{
    Exact,
    KeyframeForward,
    KeyframeBackward,
};

// Host-side outcome for one relative seek command. Boundary completion is kept
// separate from an ordinary seek so the player can emit its normal clean-EOF
// lifecycle without asking FFmpeg to seek to a timestamp at/past the duration.
enum class RelativeSeekResult : uint8_t
{
    Applied,
    IgnoredAtStart,
    CompletedAtEnd,
};

struct RelativeSeekDecision
{
    RelativeSeekResult result = RelativeSeekResult::Applied;
    double target = 0.0;
};

// Pure boundary policy for relative commands. `previousTarget` is the most recent
// requested target, not the live clock: after the first held-key step reaches zero,
// the audio clock can advance slightly before the next OS repeat arrives. Remembering
// that zero target prevents those repeats from continually restarting the pipeline.
constexpr RelativeSeekDecision DecideRelativeSeek(
    double base, double delta, double duration, bool autoRepeat, double previousTarget
)
{
    const double rawTarget = base + delta;
    if (delta > 0.0 && duration > 0.0 && rawTarget >= duration)
    {
        return {RelativeSeekResult::CompletedAtEnd, duration};
    }
    if (delta < 0.0 && rawTarget <= 0.0)
    {
        if (base <= 0.0 || (autoRepeat && previousTarget <= 0.0))
        {
            return {RelativeSeekResult::IgnoredAtStart, 0.0};
        }
        return {RelativeSeekResult::Applied, 0.0};
    }

    double target = rawTarget < 0.0 ? 0.0 : rawTarget;
    if (duration > 0.0 && target > duration)
    {
        target = duration;
    }
    return {RelativeSeekResult::Applied, target};
}

// skipPts sentinel meaning "present straight from the landed keyframe".
inline constexpr double kSeekNoSkipPts = -1e18;

constexpr SeekKind DecideSeekKind(SeekPrecisionMode mode, bool relative, double delta, bool hasVideo)
{
    switch (mode)
    {
    case SeekPrecisionMode::Exact:
        return SeekKind::Exact;
    case SeekPrecisionMode::Keyframe:
        return relative && delta < 0.0 ? SeekKind::KeyframeBackward
               : relative              ? SeekKind::KeyframeForward
                                       : SeekKind::KeyframeBackward;
    case SeekPrecisionMode::Smart:
        if (!relative || !hasVideo)
        {
            return SeekKind::Exact;
        }
        return delta < 0.0 ? SeekKind::KeyframeBackward : SeekKind::KeyframeForward;
    }
    return SeekKind::Exact;
}

// The two knobs a decided seek turns on the demuxer/worker side.
struct SeekPlan
{
    bool backwardFlag; // av_seek_frame AVSEEK_FLAG_BACKWARD (land at/before target)
    double skipPts;    // workers discard frames with pts below this (kSeekNoSkipPts = none)
};

constexpr SeekPlan BuildSeekPlan(SeekKind kind, double target)
{
    switch (kind)
    {
    case SeekKind::Exact:
        return {true, target};
    case SeekKind::KeyframeForward:
        return {false, kSeekNoSkipPts};
    case SeekKind::KeyframeBackward:
        return {true, kSeekNoSkipPts};
    }
    return {true, target};
}

// Setting-string round trip ("smart" / "exact" / "keyframe"); unknown → Smart.
constexpr const char* SeekPrecisionModeName(SeekPrecisionMode mode)
{
    switch (mode)
    {
    case SeekPrecisionMode::Exact:
        return "exact";
    case SeekPrecisionMode::Keyframe:
        return "keyframe";
    case SeekPrecisionMode::Smart:
        break;
    }
    return "smart";
}

inline SeekPrecisionMode SeekPrecisionModeFromString(const char* s)
{
    if (s && std::strcmp(s, "exact") == 0)
    {
        return SeekPrecisionMode::Exact;
    }
    if (s && std::strcmp(s, "keyframe") == 0)
    {
        return SeekPrecisionMode::Keyframe;
    }
    return SeekPrecisionMode::Smart;
}
