#include "FFmpegFilters.h"

#include "QtTestRunner.h"

#include <QtTest/QtTest>

class FFmpegFiltersTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void DefaultParamsProduceLegacyLimiterChain()
    {
        const std::string f = BuildAudioNormalizeGraph(AudioNormalizeParams{});
        QVERIFY(
            (f) == ("alimiter=level_in=10.000000:level_out=1.000000:limit=1.000000:attack=5.000000:release=8000.000000")
        );
    }

    void LimiterParamsAreReflected()
    {
        AudioNormalizeParams p;
        p.limiterLevelIn = 2.5f;
        p.limiterLevelOut = 0.8f;
        p.limiterLimit = 0.9f;
        p.limiterAttack = 7.5f;
        p.limiterRelease = 250.f;
        const std::string f = BuildAudioNormalizeGraph(p);
        QVERIFY((f.find("level_in=2.500000")) != (std::string::npos));
        QVERIFY((f.find("level_out=0.800000")) != (std::string::npos));
        QVERIFY((f.find("limit=0.900000")) != (std::string::npos));
        QVERIFY((f.find("attack=7.500000")) != (std::string::npos));
        QVERIFY((f.find("release=250.000000")) != (std::string::npos));
    }

    void DynamicNormalizerProducesExpectedChain()
    {
        AudioNormalizeParams p;
        p.algorithm = AudioNormalizeAlgorithm::DynamicNormalizer;
        const std::string f = BuildAudioNormalizeGraph(p);
        QVERIFY(
            (f) == ("dynaudnorm=f=100:g=5:p=0.950000:m=5.000000"
                    ",asoftclip=type=tanh,volume=1.500000")
        );
    }

    void GaussSizeIsForcedOdd()
    {
        AudioNormalizeParams p;
        p.algorithm = AudioNormalizeAlgorithm::DynamicNormalizer;
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
        p.algorithm = AudioNormalizeAlgorithm::DynamicNormalizer;
        p.frameLen = 250;
        QVERIFY((BuildAudioNormalizeGraph(p).find("dynaudnorm=f=250")) != (std::string::npos));
    }

    void AlwaysIncludesSoftClipSafetyNet()
    {
        AudioNormalizeParams p;
        p.algorithm = AudioNormalizeAlgorithm::DynamicNormalizer;
        QVERIFY((BuildAudioNormalizeGraph(p).find("asoftclip=type=tanh")) != (std::string::npos));
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
