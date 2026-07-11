#include "FFmpegVideoConfig.h"

#include "QtTestRunner.h"

#include <QtTest/QtTest>

class FFmpegVideoConfigTests final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void FirstFrameAnnouncesConfiguration()
    {
        FFmpegVideoConfigTracker tracker;
        QVERIFY(tracker.Update(1920, 1080));
    }

    void UnchangedFrameAfterWorkerRestartDoesNotAnnounce()
    {
        FFmpegVideoConfigTracker tracker;
        QVERIFY(tracker.Update(1920, 1080));
        QVERIFY(!tracker.Update(1920, 1080));
    }

    void ResolutionChangeAnnouncesConfiguration()
    {
        FFmpegVideoConfigTracker tracker;
        QVERIFY(tracker.Update(1920, 1080));
        QVERIFY(tracker.Update(1280, 720));
    }

    void NewFileResetAllowsInitialAnnouncement()
    {
        FFmpegVideoConfigTracker tracker;
        QVERIFY(tracker.Update(1920, 1080));
        tracker.Reset();
        QVERIFY(tracker.Update(1920, 1080));
    }
};

namespace
{
const ::framelift::test::Registrar<FFmpegVideoConfigTests> kRegisterFFmpegVideoConfigTests{"FFmpegVideoConfigTests"};
}

#include "FFmpegVideoConfigTests.moc"
