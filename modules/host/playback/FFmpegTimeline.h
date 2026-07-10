#pragma once

#include <cmath>
#include <optional>

// FFmpeg reports packet timestamps on a container timeline that may start well
// away from zero (notably MPEG-TS). The player exposes a session-relative
// timeline to its UI, history, clocks, and libass instead.
namespace FFmpegTimeline
{
[[nodiscard]] inline double SelectStart(
    std::optional<double> containerStart, std::optional<double> primaryStreamStart
) noexcept
{
    return containerStart.value_or(primaryStreamStart.value_or(0.0));
}

[[nodiscard]] inline double ToRelative(double rawSeconds, double startSeconds) noexcept
{
    return rawSeconds - startSeconds;
}

[[nodiscard]] inline double ToDemux(double relativeSeconds, double startSeconds) noexcept
{
    return relativeSeconds + startSeconds;
}

[[nodiscard]] inline long long ToRelativeMilliseconds(long long rawMilliseconds, double startSeconds) noexcept
{
    return rawMilliseconds - static_cast<long long>(std::llround(startSeconds * 1000.0));
}
} // namespace FFmpegTimeline
