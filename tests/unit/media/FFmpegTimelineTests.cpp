#include "FFmpegTimeline.h"

#include "QtTestRunner.h"

#include <QtTest/QtTest>

class FFmpegTimelineTests final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void ContainerStartTakesPrecedence()
    {
        QCOMPARE(FFmpegTimeline::SelectStart(1401.38, 1401.4), 1401.38);
    }

    void StreamStartIsFallback()
    {
        QCOMPARE(FFmpegTimeline::SelectStart(std::nullopt, 12.5), 12.5);
    }

    void MissingStartsUseZero()
    {
        QCOMPARE(FFmpegTimeline::SelectStart(std::nullopt, std::nullopt), 0.0);
    }

    void RawPtsBecomesPlaybackRelative()
    {
        QCOMPARE(FFmpegTimeline::ToRelative(1409.38, 1401.38), 8.0);
    }

    void RelativeSeekBecomesDemuxTimestamp()
    {
        QCOMPARE(FFmpegTimeline::ToDemux(8.0, 1401.38), 1409.38);
    }

    void SubtitleCueMillisecondsBecomePlaybackRelative()
    {
        QCOMPARE(FFmpegTimeline::ToRelativeMilliseconds(1409380, 1401.38), 8000LL);
    }
};

namespace
{
const ::framelift::test::Registrar<FFmpegTimelineTests> kRegisterFFmpegTimelineTests{"FFmpegTimelineTests"};
}

#include "FFmpegTimelineTests.moc"
