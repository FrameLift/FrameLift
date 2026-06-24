// Pure-logic tests for the Windows shell integration's mapping helpers
// (modules/platform/win-shell/ProgressMapping.h). The Win32/COM seams
// (TaskbarProgress.cpp / ToastNotifier.cpp) are not part of this suite — they
// need Windows and are verified manually; only the platform-independent
// permille/state mapping and the error-toast debounce are tested here so they
// run on the Linux CI runner.

#include "ProgressMapping.h"

#include <gtest/gtest.h>

namespace
{

PlaybackSnapshot Playing(double pos, double dur)
{
    return PlaybackSnapshot{.timePos = pos, .duration = dur};
}

} // namespace

// ── ProgressPermille ──────────────────────────────────────────────────────────

TEST(WinShellProgress, PermilleAtStartIsZero)
{
    EXPECT_EQ(ProgressPermille(Playing(0.0, 100.0)), 0);
}

TEST(WinShellProgress, PermilleAtHalfIs500)
{
    EXPECT_EQ(ProgressPermille(Playing(50.0, 100.0)), 500);
}

TEST(WinShellProgress, PermilleAtEndIs1000)
{
    EXPECT_EQ(ProgressPermille(Playing(100.0, 100.0)), 1000);
}

TEST(WinShellProgress, PermilleClampsAboveDuration)
{
    EXPECT_EQ(ProgressPermille(Playing(200.0, 100.0)), 1000);
}

TEST(WinShellProgress, PermilleClampsNegativePosition)
{
    EXPECT_EQ(ProgressPermille(Playing(-5.0, 100.0)), 0);
}

TEST(WinShellProgress, PermilleZeroWhenDurationUnknown)
{
    EXPECT_EQ(ProgressPermille(Playing(10.0, 0.0)), 0);
    EXPECT_EQ(ProgressPermille(Playing(10.0, -1.0)), 0);
}

TEST(WinShellProgress, PermilleRounds)
{
    // 1/3 of 1000 = 333.33 → 333; 2/3 = 666.66 → 667.
    EXPECT_EQ(ProgressPermille(Playing(1.0, 3.0)), 333);
    EXPECT_EQ(ProgressPermille(Playing(2.0, 3.0)), 667);
}

// ── MapState ──────────────────────────────────────────────────────────────────

TEST(WinShellState, ErrorTakesPrecedence)
{
    PlaybackSnapshot s = Playing(10.0, 100.0);
    s.errored = true;
    s.paused = true;
    s.idle = true;
    s.eof = true;
    EXPECT_EQ(MapState(s), ProgressState::Error);
}

TEST(WinShellState, IdleClearsBar)
{
    PlaybackSnapshot s{};
    s.idle = true;
    EXPECT_EQ(MapState(s), ProgressState::NoProgress);
}

TEST(WinShellState, EofClearsBar)
{
    PlaybackSnapshot s = Playing(100.0, 100.0);
    s.eof = true;
    EXPECT_EQ(MapState(s), ProgressState::NoProgress);
}

TEST(WinShellState, UnknownDurationIsIndeterminate)
{
    // Playing a live stream / audio of unknown length.
    EXPECT_EQ(MapState(Playing(10.0, 0.0)), ProgressState::Indeterminate);
}

TEST(WinShellState, PausedWhenPaused)
{
    PlaybackSnapshot s = Playing(10.0, 100.0);
    s.paused = true;
    EXPECT_EQ(MapState(s), ProgressState::Paused);
}

TEST(WinShellState, NormalWhilePlaying)
{
    EXPECT_EQ(MapState(Playing(10.0, 100.0)), ProgressState::Normal);
}

// ── ProgressChanged (throttle) ──────────────────────────────────────────────────

TEST(WinShellThrottle, NoChangeSuppressed)
{
    EXPECT_FALSE(ProgressChanged(500, ProgressState::Normal, 500, ProgressState::Normal));
}

TEST(WinShellThrottle, PermilleChangeAllows)
{
    EXPECT_TRUE(ProgressChanged(501, ProgressState::Normal, 500, ProgressState::Normal));
}

TEST(WinShellThrottle, StateChangeAllows)
{
    EXPECT_TRUE(ProgressChanged(500, ProgressState::Paused, 500, ProgressState::Normal));
}

// ── ShouldNotifyError (debounce) ─────────────────────────────────────────────────

TEST(WinShellDebounce, DifferentFileAlwaysNotifies)
{
    EXPECT_TRUE(ShouldNotifyError(/*sameFileAsLast=*/false, /*secondsSinceLast=*/0.1));
}

TEST(WinShellDebounce, SameFileWithinWindowSuppressed)
{
    EXPECT_FALSE(ShouldNotifyError(/*sameFileAsLast=*/true, /*secondsSinceLast=*/0.5));
}

TEST(WinShellDebounce, SameFileAfterWindowNotifies)
{
    EXPECT_TRUE(ShouldNotifyError(/*sameFileAsLast=*/true, /*secondsSinceLast=*/2.0));
}
