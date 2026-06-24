// Unit tests for the FFmpeg backend's pure A/V-sync math (issue #8, Phase 3).
// FFmpegClock.h is deliberately free of libav/SDL so it builds in the standalone
// native test suite that has neither dependency.

#include "FFmpegClock.h"

#include <gtest/gtest.h>

// ── ComputeMasterClock ─────────────────────────────────────────────────────────

TEST(FFmpegClockTests, MasterClockSubtractsQueuedBacklog)
{
    // 2 s worth of audio bytes still queued at 192000 bytes/s (48k * 2ch * 4B/2).
    // Here use a round rate: 96000 bytes/s, 96000 queued = 1.0 s backlog.
    EXPECT_DOUBLE_EQ(ComputeMasterClock(10.0, 96000, 96000), 9.0);
}

TEST(FFmpegClockTests, MasterClockEqualsPtsWhenNothingQueued)
{
    EXPECT_DOUBLE_EQ(ComputeMasterClock(5.5, 0, 192000), 5.5);
}

TEST(FFmpegClockTests, MasterClockGuardsZeroBytesPerSec)
{
    // Device not open yet — fall back to the last queued pts, no divide by zero.
    EXPECT_DOUBLE_EQ(ComputeMasterClock(3.25, 12345, 0), 3.25);
    EXPECT_DOUBLE_EQ(ComputeMasterClock(3.25, 12345, -1), 3.25);
}

// ── DecideFrame ────────────────────────────────────────────────────────────────

TEST(FFmpegClockTests, SubtitleRenderClockUsesMasterWithoutSeekOverride)
{
    EXPECT_DOUBLE_EQ(SelectSubtitleRenderClock(/*master*/ 12.5, /*override*/ false, /*seekTarget*/ 42.0), 12.5);
}

TEST(FFmpegClockTests, SubtitleRenderClockUsesSeekTargetWithOverride)
{
    EXPECT_DOUBLE_EQ(SelectSubtitleRenderClock(/*master*/ 0.0, /*override*/ true, /*seekTarget*/ 42.0), 42.0);
}

TEST(FFmpegClockTests, SubtitleRenderClockReturnsToMasterAfterOverrideClears)
{
    EXPECT_DOUBLE_EQ(SelectSubtitleRenderClock(/*master*/ 42.5, /*override*/ false, /*seekTarget*/ 12.0), 42.5);
}

TEST(FFmpegClockTests, FutureFrameWaits)
{
    EXPECT_EQ(DecideFrame(/*framePts*/ 5.0, /*master*/ 4.5, /*thresh*/ 0.1), FrameAction::Wait);
}

TEST(FFmpegClockTests, OnTimeFramePresents)
{
    EXPECT_EQ(DecideFrame(5.0, 5.0, 0.1), FrameAction::Present);
}

TEST(FFmpegClockTests, SlightlyLateFrameStillPresents)
{
    // 50 ms late, under the 100 ms drop threshold.
    EXPECT_EQ(DecideFrame(5.0, 5.05, 0.1), FrameAction::Present);
}

TEST(FFmpegClockTests, FrameExactlyAtThresholdPresents)
{
    // Boundary: diff == -threshold is the last value that still presents.
    EXPECT_EQ(DecideFrame(5.0, 5.1, 0.1), FrameAction::Present);
}

TEST(FFmpegClockTests, TooLateFrameDrops)
{
    EXPECT_EQ(DecideFrame(5.0, 5.2, 0.1), FrameAction::Drop);
}

// ── IsMistimedFrame ────────────────────────────────────────────────────────────

TEST(FFmpegClockTests, OnTimeFrameNotMistimed)
{
    // Exactly on the clock — zero lag is within any tolerance.
    EXPECT_FALSE(IsMistimedFrame(/*framePts*/ 5.0, /*master*/ 5.0, /*tol*/ 0.008));
}

TEST(FFmpegClockTests, EarlyFrameNotMistimed)
{
    // Frame ahead of the clock (would Wait) is never mistimed.
    EXPECT_FALSE(IsMistimedFrame(5.0, 4.99, 0.008));
}

TEST(FFmpegClockTests, SlightlyLateWithinToleranceNotMistimed)
{
    // 5 ms late, under an 8 ms (~half a 60fps frame) tolerance — sub-frame jitter.
    EXPECT_FALSE(IsMistimedFrame(5.0, 5.005, 0.008));
}

TEST(FFmpegClockTests, LateBeyondToleranceIsMistimed)
{
    // 12 ms late, past the 8 ms tolerance — a genuinely missed slot.
    EXPECT_TRUE(IsMistimedFrame(5.0, 5.012, 0.008));
}

// ── ClampSeekTarget ────────────────────────────────────────────────────────────

TEST(FFmpegClockTests, ClampSeekNegativeGoesToZero)
{
    EXPECT_DOUBLE_EQ(ClampSeekTarget(-3.0, 120.0), 0.0);
}

TEST(FFmpegClockTests, ClampSeekBeyondEndGoesToDuration)
{
    EXPECT_DOUBLE_EQ(ClampSeekTarget(200.0, 120.0), 120.0);
}

TEST(FFmpegClockTests, ClampSeekInRangeUnchanged)
{
    EXPECT_DOUBLE_EQ(ClampSeekTarget(42.5, 120.0), 42.5);
}

TEST(FFmpegClockTests, ClampSeekUnknownDurationKeepsTarget)
{
    // Duration unknown (<= 0): only the lower bound is enforced.
    EXPECT_DOUBLE_EQ(ClampSeekTarget(500.0, 0.0), 500.0);
    EXPECT_DOUBLE_EQ(ClampSeekTarget(-1.0, 0.0), 0.0);
}
