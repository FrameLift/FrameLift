// Unit tests for FFmpegSeekPlan.h — the seek precision policy (mpv hr-seek
// semantics): which requests snap to keyframes vs decode exactly to the target.

#include "FFmpegSeekPlan.h"

#include "QtTestRunner.h"

#include <QtTest/QtTest>

class FFmpegSeekPlanTests final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void SmartMatchesMpvHrSeekDefault()
    {
        // Relative seeks on video snap to keyframes, direction-aware.
        QVERIFY(DecideSeekKind(SeekPrecisionMode::Smart, true, 5.0, true) == SeekKind::KeyframeForward);
        QVERIFY(DecideSeekKind(SeekPrecisionMode::Smart, true, -5.0, true) == SeekKind::KeyframeBackward);
        QVERIFY(DecideSeekKind(SeekPrecisionMode::Smart, true, 0.0, true) == SeekKind::KeyframeForward);
        // Absolute seeks (seek bar, resume, chapter) are exact.
        QVERIFY(DecideSeekKind(SeekPrecisionMode::Smart, false, 0.0, true) == SeekKind::Exact);
        // Audio-only files seek exactly even for relative seeks (hr-seek=default).
        QVERIFY(DecideSeekKind(SeekPrecisionMode::Smart, true, 5.0, false) == SeekKind::Exact);
        QVERIFY(DecideSeekKind(SeekPrecisionMode::Smart, false, 0.0, false) == SeekKind::Exact);
    }

    void ExactIsAlwaysExact()
    {
        for (bool relative : {true, false})
        {
            for (bool hasVideo : {true, false})
            {
                QVERIFY(DecideSeekKind(SeekPrecisionMode::Exact, relative, 5.0, hasVideo) == SeekKind::Exact);
                QVERIFY(DecideSeekKind(SeekPrecisionMode::Exact, relative, -5.0, hasVideo) == SeekKind::Exact);
            }
        }
    }

    void KeyframeAlwaysSnaps()
    {
        QVERIFY(DecideSeekKind(SeekPrecisionMode::Keyframe, true, 5.0, true) == SeekKind::KeyframeForward);
        QVERIFY(DecideSeekKind(SeekPrecisionMode::Keyframe, true, -5.0, true) == SeekKind::KeyframeBackward);
        // Absolute keyframe seeks land at/before so the seek never overshoots.
        QVERIFY(DecideSeekKind(SeekPrecisionMode::Keyframe, false, 0.0, true) == SeekKind::KeyframeBackward);
        QVERIFY(DecideSeekKind(SeekPrecisionMode::Keyframe, true, 5.0, false) == SeekKind::KeyframeForward);
    }

    void PlansSetFlagAndSkipPts()
    {
        const SeekPlan exact = BuildSeekPlan(SeekKind::Exact, 42.5);
        QVERIFY(exact.backwardFlag); // land at/before, then decode-discard to target
        QCOMPARE(exact.skipPts, 42.5);

        const SeekPlan fwd = BuildSeekPlan(SeekKind::KeyframeForward, 42.5);
        QVERIFY(!fwd.backwardFlag); // land at/after so a +N step always advances
        QCOMPARE(fwd.skipPts, kSeekNoSkipPts);

        const SeekPlan back = BuildSeekPlan(SeekKind::KeyframeBackward, 42.5);
        QVERIFY(back.backwardFlag);
        QCOMPARE(back.skipPts, kSeekNoSkipPts);
    }

    void RelativeSeekCompletesAtKnownEnd()
    {
        const auto crossing = DecideRelativeSeek(118.0, 5.0, 120.0, false, 0.0);
        QVERIFY(crossing.result == RelativeSeekResult::CompletedAtEnd);
        QCOMPARE(crossing.target, 120.0);

        const auto exact = DecideRelativeSeek(115.0, 5.0, 120.0, false, 0.0);
        QVERIFY(exact.result == RelativeSeekResult::CompletedAtEnd);
    }

    void RelativeSeekReachesStartOnceDuringRepeat()
    {
        const auto first = DecideRelativeSeek(3.0, -5.0, 120.0, false, 42.0);
        QVERIFY(first.result == RelativeSeekResult::Applied);
        QCOMPARE(first.target, 0.0);

        // The live audio clock may already have crept forward after the first seek;
        // the remembered zero target still suppresses the OS repeat.
        const auto repeated = DecideRelativeSeek(0.15, -5.0, 120.0, true, 0.0);
        QVERIFY(repeated.result == RelativeSeekResult::IgnoredAtStart);
    }

    void FreshBackwardPressWorksAfterPlaybackAdvances()
    {
        const auto fresh = DecideRelativeSeek(3.0, -5.0, 120.0, false, 0.0);
        QVERIFY(fresh.result == RelativeSeekResult::Applied);
        QCOMPARE(fresh.target, 0.0);
    }

    void RelativeSeekAtStartIsIgnored()
    {
        const auto decision = DecideRelativeSeek(0.0, -5.0, 120.0, false, 0.0);
        QVERIFY(decision.result == RelativeSeekResult::IgnoredAtStart);
    }

    void UnknownDurationNeverCompletesFromRelativeSeek()
    {
        const auto decision = DecideRelativeSeek(100.0, 60.0, 0.0, true, 100.0);
        QVERIFY(decision.result == RelativeSeekResult::Applied);
        QCOMPARE(decision.target, 160.0);
    }

    void ModeStringsRoundTrip()
    {
        for (SeekPrecisionMode mode : {SeekPrecisionMode::Smart, SeekPrecisionMode::Exact, SeekPrecisionMode::Keyframe})
        {
            QVERIFY(SeekPrecisionModeFromString(SeekPrecisionModeName(mode)) == mode);
        }
        QVERIFY(SeekPrecisionModeFromString("garbage") == SeekPrecisionMode::Smart);
        QVERIFY(SeekPrecisionModeFromString(nullptr) == SeekPrecisionMode::Smart);
    }
};

namespace
{
const ::framelift::test::Registrar<FFmpegSeekPlanTests> kRegisterFFmpegSeekPlanTests{"FFmpegSeekPlanTests"};
}

#include "FFmpegSeekPlanTests.moc"
