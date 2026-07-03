#include "TestMediaGenerator.h"
#include "TestMediaSpec.h"

#include "QtTestRunner.h"

#include <QtTest/QtTest>

class TestMediaSpecTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void DurationOnlyKeepsOtherDefaults()
    {
        const auto s = ParseTestMediaSpec("10s");
        QVERIFY(s.has_value());
        QCOMPARE(s->durationSeconds, 10);
        QCOMPARE(s->width, 1280);
        QCOMPARE(s->height, 720);
        QCOMPARE(s->codec, std::string("h264"));
        QVERIFY(s->subtitles == TestSubtitleKind::None);
        QVERIFY(s->audio);
    }

    void TokenOrderDoesNotMatter()
    {
        const auto a = ParseTestMediaSpec("30s-1080p-h264-srt");
        const auto b = ParseTestMediaSpec("srt-h264-1080p-30s");
        QVERIFY(a.has_value() && b.has_value());
        QCOMPARE(CanonicalTestMediaName(*a), CanonicalTestMediaName(*b));
        QCOMPARE(CanonicalTestMediaName(*a), std::string("30s-1920x1080-h264-audio-srt"));
    }

    void NamedResolutions()
    {
        const auto fourK = ParseTestMediaSpec("4k");
        QVERIFY(fourK.has_value());
        QCOMPARE(fourK->width, 3840);
        QCOMPARE(fourK->height, 2160);

        const auto p360 = ParseTestMediaSpec("360p");
        QVERIFY(p360.has_value());
        QCOMPARE(p360->width, 640);
        QCOMPARE(p360->height, 360);
    }

    void ExplicitResolution()
    {
        const auto s = ParseTestMediaSpec("640x360");
        QVERIFY(s.has_value());
        QCOMPARE(s->width, 640);
        QCOMPARE(s->height, 360);
    }

    void RejectsBadResolutions()
    {
        QVERIFY(!ParseTestMediaSpec("0x100").has_value());
        QVERIFY(!ParseTestMediaSpec("axb").has_value());
        QVERIFY(!ParseTestMediaSpec("641x360").has_value()); // odd width, yuv420p needs 2x2 alignment
        QVERIFY(!ParseTestMediaSpec("99999x100").has_value());
    }

    void MinutesDuration()
    {
        const auto s = ParseTestMediaSpec("2m");
        QVERIFY(s.has_value());
        QCOMPARE(s->durationSeconds, 120);
    }

    void RejectsBadDurations()
    {
        QVERIFY(!ParseTestMediaSpec("0s").has_value());
        QVERIFY(!ParseTestMediaSpec("10").has_value()); // bare number: ambiguous, needs a unit
        QVERIFY(!ParseTestMediaSpec("10x").has_value());
        QVERIFY(!ParseTestMediaSpec("2h").has_value());
        QVERIFY(!ParseTestMediaSpec("120m").has_value()); // over the 1h cap
    }

    void CodecAliasCanonicalizes()
    {
        const auto s = ParseTestMediaSpec("10s-h265");
        QVERIFY(s.has_value());
        QCOMPARE(s->codec, std::string("hevc"));
        QCOMPARE(CanonicalTestMediaName(*s), std::string("10s-1280x720-hevc-audio"));
    }

    void NoAudioToken()
    {
        const auto s = ParseTestMediaSpec("10s-noaudio");
        QVERIFY(s.has_value());
        QVERIFY(!s->audio);
        QCOMPARE(CanonicalTestMediaName(*s), std::string("10s-1280x720-h264-noaudio"));
    }

    void CaseInsensitive()
    {
        const auto s = ParseTestMediaSpec("10S-1080P-H264-SRT");
        QVERIFY(s.has_value());
        QCOMPARE(s->height, 1080);
        QVERIFY(s->subtitles == TestSubtitleKind::Srt);
    }

    void RejectsDuplicateDimensions()
    {
        QVERIFY(!ParseTestMediaSpec("10s-20s").has_value());
        QVERIFY(!ParseTestMediaSpec("720p-1080p").has_value());
        QVERIFY(!ParseTestMediaSpec("720p-640x360").has_value());
        QVERIFY(!ParseTestMediaSpec("h264-hevc").has_value());
        QVERIFY(!ParseTestMediaSpec("srt-ass").has_value());
        QVERIFY(!ParseTestMediaSpec("noaudio-noaudio").has_value());
    }

    void RejectsUnknownAndEmpty()
    {
        QVERIFY(!ParseTestMediaSpec("").has_value());
        QVERIFY(!ParseTestMediaSpec("30s-webm").has_value());
        QVERIFY(!ParseTestMediaSpec("-30s").has_value());
        QVERIFY(!ParseTestMediaSpec("30s-").has_value());
        QVERIFY(!ParseTestMediaSpec("30s--srt").has_value());
    }

    void SubtitleCuesCoverDuration()
    {
        const std::string cues = BuildTestSubtitleCues(6);
        QVERIFY(cues.find("1\n00:00:00,000 --> 00:00:02,000\nFrameLift 00:00\n") != std::string::npos);
        QVERIFY(cues.find("2\n00:00:02,000 --> 00:00:04,000\nFrameLift 00:02\n") != std::string::npos);
        QVERIFY(cues.find("3\n00:00:04,000 --> 00:00:06,000\nFrameLift 00:04\n") != std::string::npos);
        QVERIFY(cues.find("\n\n4\n") == std::string::npos); // exactly three cues for 6 seconds
    }

    void LastCueClampsToDuration()
    {
        // Odd duration: the final cue must end at the clip end, not past it.
        const std::string cues = BuildTestSubtitleCues(5);
        QVERIFY(cues.find("00:00:04,000 --> 00:00:05,000") != std::string::npos);
    }

    void FfmpegArgsWithSubtitles()
    {
        const auto s = ParseTestMediaSpec("30s-1080p-h264-srt");
        QVERIFY(s.has_value());
        const QStringList args = TestMediaGenerator::BuildFfmpegArgs(*s, "out.mkv.part", "cues.srt.tmp");
        QVERIFY(args.contains("testsrc2=size=1920x1080:rate=30:duration=30"));
        QVERIFY(args.contains("sine=frequency=440:sample_rate=48000:duration=30"));
        QVERIFY(args.contains("cues.srt.tmp"));
        const int mapSub = static_cast<int>(args.indexOf("2:s"));
        QVERIFY(mapSub > 0);
        QCOMPARE(args[mapSub - 1], QString("-map"));
        QVERIFY(args.contains("-c:s"));
        QCOMPARE(args[args.indexOf("-c:s") + 1], QString("srt"));
        QCOMPARE(args.last(), QString("out.mkv.part"));
    }

    void FfmpegArgsNoAudioShiftsSubtitleInput()
    {
        const auto s = ParseTestMediaSpec("10s-noaudio-ass");
        QVERIFY(s.has_value());
        const QStringList args = TestMediaGenerator::BuildFfmpegArgs(*s, "out.mkv.part", "cues.srt.tmp");
        QVERIFY(!args.contains("-c:a"));
        QVERIFY(!args.contains("1:a"));
        QVERIFY(args.contains("1:s"));
        QCOMPARE(args[args.indexOf("-c:s") + 1], QString("ass"));
    }

    void FfmpegArgsPlain()
    {
        const auto s = ParseTestMediaSpec("10s");
        QVERIFY(s.has_value());
        const QStringList args = TestMediaGenerator::BuildFfmpegArgs(*s, "out.mkv.part", QString());
        QVERIFY(!args.contains("-c:s"));
        QVERIFY(args.contains("-c:a"));
        QCOMPARE(args[args.indexOf("-c:v") + 1], QString("libx264"));
        // .part hides the container from extension inference, so it must be explicit.
        const int fmt = static_cast<int>(args.lastIndexOf("-f"));
        QCOMPARE(args[fmt + 1], QString("matroska"));
    }
};

namespace
{
const ::framelift::test::Registrar<TestMediaSpecTest> kRegisterTestMediaSpecTest{"TestMediaSpecTest"};
}

#include "TestMediaSpecTests.moc"
