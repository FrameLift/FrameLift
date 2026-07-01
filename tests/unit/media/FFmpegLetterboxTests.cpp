// Unit tests for the FFmpeg backend's letterbox/pillarbox math (issue #8, Phase 5).
// FFmpegLetterbox.h is free of libav/GL so it builds in the standalone native suite.

#include "FFmpegLetterbox.h"

#include "QtTestRunner.h"

#include <QtTest/QtTest>

class FFmpegLetterboxTests final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void ExactAspectFillsFramebuffer()
    {
        const LetterboxRect r = ComputeLetterbox(1920, 1080, 1920, 1080);
        QVERIFY((r.x) == (0));
        QVERIFY((r.y) == (0));
        QVERIFY((r.w) == (1920));
        QVERIFY((r.h) == (1080));
    }

    void SameAspectDifferentScaleFills()
    {
        // 16:9 video into a 16:9 framebuffer of a different size — no bars.
        const LetterboxRect r = ComputeLetterbox(1280, 720, 1920, 1080);
        QVERIFY((r.x) == (0));
        QVERIFY((r.y) == (0));
        QVERIFY((r.w) == (1280));
        QVERIFY((r.h) == (720));
    }

    void WideFramebufferPillarboxes()
    {
        // 4:3 video in a 16:9 window → bars left/right, full height.
        const LetterboxRect r = ComputeLetterbox(1600, 900, 640, 480);
        QVERIFY((r.h) == (900));
        QVERIFY((r.w) == (1200)); // 900 * 4/3
        QVERIFY((r.x) == (200));  // (1600 - 1200) / 2
        QVERIFY((r.y) == (0));
    }

    void TallFramebufferLetterboxes()
    {
        // 16:9 video in a 4:3 window → bars top/bottom, full width.
        const LetterboxRect r = ComputeLetterbox(800, 600, 1920, 1080);
        QVERIFY((r.w) == (800));
        QVERIFY((r.h) == (450)); // 800 * 9/16
        QVERIFY((r.x) == (0));
        QVERIFY((r.y) == (75)); // (600 - 450) / 2
    }

    void RoundsToNearestPixel()
    {
        // 1:1 video into 101x100 → width rounds 100*1 = 100, centered with 1px bar.
        const LetterboxRect r = ComputeLetterbox(101, 100, 100, 100);
        QVERIFY((r.w) == (100));
        QVERIFY((r.h) == (100));
        QVERIFY((r.x) == (0)); // (101 - 100) / 2 == 0 (integer)
    }

    void DegenerateInputsClampToFramebuffer()
    {
        const LetterboxRect a = ComputeLetterbox(640, 480, 0, 0);
        QVERIFY((a.w) == (640));
        QVERIFY((a.h) == (480));

        const LetterboxRect b = ComputeLetterbox(0, 0, 1920, 1080);
        QVERIFY((b.w) == (0));
        QVERIFY((b.h) == (0));
    }
};

namespace
{
const ::framelift::test::Registrar<FFmpegLetterboxTests> kRegisterFFmpegLetterboxTests{"FFmpegLetterboxTests"};
}

#include "FFmpegLetterboxTests.moc"
