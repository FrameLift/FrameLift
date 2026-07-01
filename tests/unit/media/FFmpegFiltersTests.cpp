#include "FFmpegFilters.h"

#include "QtTestRunner.h"

#include <QtTest/QtTest>

class FFmpegFiltersTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void DefaultParamsProduceExpectedChain()
    {
        const std::string f = BuildAudioNormalizeGraph(AudioNormalizeParams{});
        // The libavfilter chain description, without any "lavfi=[...]" wrapper
        // (abuffer/abuffersink are added when the graph is built).
        QVERIFY(
            (f) == ("dynaudnorm=f=100:g=5:p=0.950000:m=5.000000"
                    ",asoftclip=type=tanh,volume=1.500000")
        );
    }

    void GaussSizeIsForcedOdd()
    {
        AudioNormalizeParams p;
        p.gaussSize = 4;
        QVERIFY((BuildAudioNormalizeGraph(p).find(":g=5")) != (std::string::npos));
        p.gaussSize = 6;
        QVERIFY((BuildAudioNormalizeGraph(p).find(":g=7")) != (std::string::npos));
        p.gaussSize = 5; // already odd — unchanged
        QVERIFY((BuildAudioNormalizeGraph(p).find(":g=5")) != (std::string::npos));
    }

    void FrameLengthIsReflected()
    {
        AudioNormalizeParams p;
        p.frameLen = 250;
        QVERIFY((BuildAudioNormalizeGraph(p).find("dynaudnorm=f=250")) != (std::string::npos));
    }

    void AlwaysIncludesSoftClipSafetyNet()
    {
        QVERIFY((BuildAudioNormalizeGraph(AudioNormalizeParams{}).find("asoftclip=type=tanh")) != (std::string::npos));
    }

    void NoLavfiWrapper()
    {
        // The FFmpeg backend builds an avfilter graph directly; no "lavfi=[...]" wrapper is used.
        const std::string f = BuildAudioNormalizeGraph(AudioNormalizeParams{});
        QVERIFY((f.find("lavfi")) == (std::string::npos));
    }
};

namespace
{
const ::framelift::test::Registrar<FFmpegFiltersTest> kRegisterFFmpegFiltersTest{"FFmpegFiltersTest"};
}

#include "FFmpegFiltersTests.moc"
