// Unit tests for the FFmpeg backend's pure A/V-sync math (issue #8, Phase 3).
// FFmpegClock.h is deliberately free of libav/platform-audio includes so it builds
// in the standalone native test suite that has neither dependency.

#include "FFmpegClock.h"

#include "QtTestRunner.h"

#include <QtTest/QtTest>

#include <chrono>
#include <limits>

// ── ComputeMasterClock ─────────────────────────────────────────────────────────

class FFmpegClockTests final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void MasterClockSubtractsQueuedBacklog()
    {
        // 2 s worth of audio bytes still queued at 192000 bytes/s (48k * 2ch * 4B/2).
        // Here use a round rate: 96000 bytes/s, 96000 queued = 1.0 s backlog.
        QCOMPARE(ComputeMasterClock(10.0, 96000, 96000), 9.0);
    }

    void MasterClockEqualsPtsWhenNothingQueued()
    {
        QCOMPARE(ComputeMasterClock(5.5, 0, 192000), 5.5);
    }

    void MasterClockGuardsZeroBytesPerSec()
    {
        // Device not open yet — fall back to the last queued pts, no divide by zero.
        QCOMPARE(ComputeMasterClock(3.25, 12345, 0), 3.25);
        QCOMPARE(ComputeMasterClock(3.25, 12345, -1), 3.25);
    }

    // ── DecideFrame ────────────────────────────────────────────────────────────────

    void SubtitleRenderClockUsesMasterWithoutSeekOverride()
    {
        QCOMPARE(SelectSubtitleRenderClock(/*master*/ 12.5, /*override*/ false, /*seekTarget*/ 42.0), 12.5);
    }

    void SubtitleRenderClockUsesSeekTargetWithOverride()
    {
        QCOMPARE(SelectSubtitleRenderClock(/*master*/ 0.0, /*override*/ true, /*seekTarget*/ 42.0), 42.0);
    }

    void SubtitleRenderClockReturnsToMasterAfterOverrideClears()
    {
        QCOMPARE(SelectSubtitleRenderClock(/*master*/ 42.5, /*override*/ false, /*seekTarget*/ 12.0), 42.5);
    }

    void FutureFrameWaits()
    {
        QVERIFY((DecideFrame(/*framePts*/ 5.0, /*master*/ 4.5, /*thresh*/ 0.1)) == (FrameAction::Wait));
    }

    void OnTimeFramePresents()
    {
        QVERIFY((DecideFrame(5.0, 5.0, 0.1)) == (FrameAction::Present));
    }

    void SlightlyLateFrameStillPresents()
    {
        // 50 ms late, under the 100 ms drop threshold.
        QVERIFY((DecideFrame(5.0, 5.05, 0.1)) == (FrameAction::Present));
    }

    void FrameExactlyAtThresholdPresents()
    {
        // Boundary: diff == -threshold is the last value that still presents.
        QVERIFY((DecideFrame(5.0, 5.1, 0.1)) == (FrameAction::Present));
    }

    void TooLateFrameDrops()
    {
        QVERIFY((DecideFrame(5.0, 5.2, 0.1)) == (FrameAction::Drop));
    }

    // ── IsMistimedFrame ────────────────────────────────────────────────────────────

    void OnTimeFrameNotMistimed()
    {
        // Exactly on the clock — zero lag is within any tolerance.
        QVERIFY(!(IsMistimedFrame(/*framePts*/ 5.0, /*master*/ 5.0, /*tol*/ 0.008)));
    }

    void EarlyFrameNotMistimed()
    {
        // Frame ahead of the clock (would Wait) is never mistimed.
        QVERIFY(!(IsMistimedFrame(5.0, 4.99, 0.008)));
    }

    void SlightlyLateWithinToleranceNotMistimed()
    {
        // 5 ms late, under an 8 ms (~half a 60fps frame) tolerance — sub-frame jitter.
        QVERIFY(!(IsMistimedFrame(5.0, 5.005, 0.008)));
    }

    void LateBeyondToleranceIsMistimed()
    {
        // 12 ms late, past the 8 ms tolerance — a genuinely missed slot.
        QVERIFY(IsMistimedFrame(5.0, 5.012, 0.008));
    }

    // ── ClampSeekTarget ────────────────────────────────────────────────────────────

    void ClampSeekNegativeGoesToZero()
    {
        QCOMPARE(ClampSeekTarget(-3.0, 120.0), 0.0);
    }

    void ClampSeekBeyondEndGoesToDuration()
    {
        QCOMPARE(ClampSeekTarget(200.0, 120.0), 120.0);
    }

    void ClampSeekInRangeUnchanged()
    {
        QCOMPARE(ClampSeekTarget(42.5, 120.0), 42.5);
    }

    void ClampSeekUnknownDurationKeepsTarget()
    {
        // Duration unknown (<= 0): only the lower bound is enforced.
        QCOMPARE(ClampSeekTarget(500.0, 0.0), 500.0);
        QCOMPARE(ClampSeekTarget(-1.0, 0.0), 0.0);
    }

    // ── VideoWallClockState ────────────────────────────────────────────────────

    void WallClockReadsZeroUntilEstablished()
    {
        VideoWallClockState clk;
        const auto now = std::chrono::steady_clock::now();
        QCOMPARE(clk.Read(false, now), 0.0);
        QCOMPARE(clk.Read(true, now), 0.0);
    }

    void WallClockAdvancesFromEstablishedPts()
    {
        VideoWallClockState clk;
        const auto t0 = std::chrono::steady_clock::now();
        QVERIFY(clk.EstablishOnce(10.0, t0));
        QCOMPARE(clk.Read(false, t0), 10.0);
        QCOMPARE(clk.Read(false, t0 + std::chrono::seconds(2)), 12.0);
        // A later frame must not re-anchor the baseline.
        QVERIFY(!clk.EstablishOnce(99.0, t0 + std::chrono::seconds(3)));
        QCOMPARE(clk.Read(false, t0 + std::chrono::seconds(4)), 14.0);
    }

    void WallClockFreezesAcrossPause()
    {
        VideoWallClockState clk;
        const auto t0 = std::chrono::steady_clock::now();
        clk.EstablishOnce(10.0, t0);
        // Pause 1s in: reads as of the pause instant no matter how much later.
        clk.OnPauseEdge(true, t0 + std::chrono::seconds(1));
        QCOMPARE(clk.Read(true, t0 + std::chrono::seconds(5)), 11.0);
        // Resume 4s later: the baseline shifts past the gap, clock continues at 11.
        clk.OnPauseEdge(false, t0 + std::chrono::seconds(5));
        QCOMPARE(clk.Read(false, t0 + std::chrono::seconds(5)), 11.0);
        QCOMPARE(clk.Read(false, t0 + std::chrono::seconds(7)), 13.0);
    }

    void WallClockResumeEdgeBeforeEstablishIsIgnored()
    {
        VideoWallClockState clk;
        const auto t0 = std::chrono::steady_clock::now();
        clk.OnPauseEdge(false, t0); // resume with no baseline: no-op, must not crash
        QVERIFY(clk.EstablishOnce(0.0, t0));
        QCOMPARE(clk.Read(false, t0 + std::chrono::seconds(1)), 1.0);
    }

    void WallClockResetRequiresReestablish()
    {
        VideoWallClockState clk;
        const auto t0 = std::chrono::steady_clock::now();
        clk.EstablishOnce(10.0, t0);
        clk.Reset();
        QCOMPARE(clk.Read(false, t0 + std::chrono::seconds(1)), 0.0);
        QVERIFY(clk.EstablishOnce(20.0, t0 + std::chrono::seconds(2)));
        QCOMPARE(clk.Read(false, t0 + std::chrono::seconds(3)), 21.0);
    }

    // ── FrameHoldWatchdog ────────────────────────────────────────────────

    void FrameHoldBreaksOnStalledClock()
    {
        const auto t0 = std::chrono::steady_clock::time_point{};
        FrameHoldWatchdog watchdog(10.0, t0);
        QVERIFY(!watchdog.ShouldBreak(10.0, t0 + std::chrono::milliseconds(499), 0.5));
        QVERIFY(watchdog.ShouldBreak(10.0, t0 + std::chrono::milliseconds(500), 0.5));
    }

    void FrameHoldKeepsWaitingWhileClockAdvances()
    {
        const auto t0 = std::chrono::steady_clock::time_point{};
        FrameHoldWatchdog watchdog(10.0, t0);
        QVERIFY(!watchdog.ShouldBreak(10.2, t0 + std::chrono::seconds(5), 0.5));
        QVERIFY(!watchdog.ShouldBreak(10.2, t0 + std::chrono::milliseconds(5499), 0.5));
    }

    void FrameHoldResetExcludesPausedTime()
    {
        const auto t0 = std::chrono::steady_clock::time_point{};
        FrameHoldWatchdog watchdog(10.0, t0);
        const auto resumed = t0 + std::chrono::seconds(30);
        watchdog.Reset(10.0, resumed);
        QVERIFY(!watchdog.ShouldBreak(10.0, resumed, 0.5));
        QVERIFY(!watchdog.ShouldBreak(10.0, resumed + std::chrono::milliseconds(499), 0.5));
        QVERIFY(watchdog.ShouldBreak(10.0, resumed + std::chrono::milliseconds(500), 0.5));
    }

    void FrameHoldBreaksWhenClockStallsAfterProgress()
    {
        const auto t0 = std::chrono::steady_clock::time_point{};
        FrameHoldWatchdog watchdog(10.0, t0);
        const auto progressed = t0 + std::chrono::milliseconds(400);
        QVERIFY(!watchdog.ShouldBreak(10.1, progressed, 0.5));
        QVERIFY(watchdog.ShouldBreak(10.1, progressed + std::chrono::milliseconds(500), 0.5));
    }

    void FrameHoldAccumulatesSubMillisecondProgress()
    {
        const auto t0 = std::chrono::steady_clock::time_point{};
        FrameHoldWatchdog watchdog(10.0, t0);
        QVERIFY(!watchdog.ShouldBreak(10.0004, t0 + std::chrono::milliseconds(200), 0.5));
        QVERIFY(!watchdog.ShouldBreak(10.0008, t0 + std::chrono::milliseconds(400), 0.5));
        // Cumulative progress reaches 1 ms and starts a fresh stall interval.
        QVERIFY(!watchdog.ShouldBreak(10.0011, t0 + std::chrono::milliseconds(499), 0.5));
        QVERIFY(!watchdog.ShouldBreak(10.0011, t0 + std::chrono::milliseconds(998), 0.5));
        QVERIFY(watchdog.ShouldBreak(10.0011, t0 + std::chrono::milliseconds(999), 0.5));
    }

    void FrameHoldBackwardClockDoesNotPostponeRecovery()
    {
        const auto t0 = std::chrono::steady_clock::time_point{};
        FrameHoldWatchdog watchdog(10.0, t0);
        QVERIFY(watchdog.ShouldBreak(9.0, t0 + std::chrono::milliseconds(500), 0.5));
    }

    // ── DecideSeekDiscard ──────────────────────────────────────────────────────

    void SeekDiscardSkipsNonRefWellBeforeTarget()
    {
        // Target 10.0s, margin 2 frames @ 24fps: packets clearly inside the
        // discard window may skip non-reference frames.
        QVERIFY(DecideSeekDiscard(5.0, 10.0, 2.0 / 24.0) == SeekDiscardMode::SkipNonRef);
    }

    void SeekDiscardDecodesInsideMargin()
    {
        // Within the margin of the target every frame decodes fully, so timestamp
        // jitter can't skip a frame the target needs.
        QVERIFY(DecideSeekDiscard(9.95, 10.0, 2.0 / 24.0) == SeekDiscardMode::DecodeAll);
        QVERIFY(DecideSeekDiscard(10.0, 10.0, 2.0 / 24.0) == SeekDiscardMode::DecodeAll);
        QVERIFY(DecideSeekDiscard(12.0, 10.0, 2.0 / 24.0) == SeekDiscardMode::DecodeAll);
    }

    void SeekDiscardInactiveForKeyframeSeeks()
    {
        // Keyframe seek / normal playback: seekSkipPts holds the -1e18 sentinel.
        QVERIFY(DecideSeekDiscard(5.0, -1e18, 2.0 / 24.0) == SeekDiscardMode::DecodeAll);
    }

    void SeekDiscardDecodesUnknownTimestamps()
    {
        // NaN pts (no pts/dts on the packet) must decode normally.
        const double nan = std::numeric_limits<double>::quiet_NaN();
        QVERIFY(DecideSeekDiscard(nan, 10.0, 2.0 / 24.0) == SeekDiscardMode::DecodeAll);
    }

    // ── ShouldReleaseAudioSeekHold ─────────────────────────────────────────────

    void AudioSeekHoldWaitsWhileVideoGrinds()
    {
        // Video not settled, nothing superseded/tearing down, within the limit: hold.
        QVERIFY(!ShouldReleaseAudioSeekHold(false, false, false, /*held*/ 0.5, /*limit*/ 2.0));
    }

    void AudioSeekHoldReleasesOnVideoSettle()
    {
        QVERIFY(ShouldReleaseAudioSeekHold(true, false, false, 0.0, 2.0));
    }

    void AudioSeekHoldReleasesOnSupersedeAndTeardown()
    {
        // The caller then drops the stale audio rather than feeding it.
        QVERIFY(ShouldReleaseAudioSeekHold(false, true, false, 0.0, 2.0));
        QVERIFY(ShouldReleaseAudioSeekHold(false, false, true, 0.0, 2.0));
    }

    void AudioSeekHoldTimesOutWhenVideoCannotPaint()
    {
        // Bounded silence: decode errors / video ending before audio must not mute
        // playback forever.
        QVERIFY(ShouldReleaseAudioSeekHold(false, false, false, /*held*/ 2.0, /*limit*/ 2.0));
        QVERIFY(!ShouldReleaseAudioSeekHold(false, false, false, /*held*/ 1.99, /*limit*/ 2.0));
    }
};

namespace
{
const ::framelift::test::Registrar<FFmpegClockTests> kRegisterFFmpegClockTests{"FFmpegClockTests"};
}

#include "FFmpegClockTests.moc"
