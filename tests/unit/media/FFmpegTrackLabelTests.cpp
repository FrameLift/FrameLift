// Unit tests for the FFmpeg backend's track-label builder (issue #8, Phase 5).
// FFmpegTrackLabel.h is free of libav so it builds in the standalone native suite.

#include "FFmpegTrackLabel.h"

#include "QtTestRunner.h"

#include <QtTest/QtTest>

#include <cstring>

class FFmpegTrackLabelTests final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void PrefersTitle()
    {
        char out[256];
        MakeTrackLabel(out, "Director Commentary", "eng", 1, nullptr);
        QVERIFY(::framelift::test::CStringEqual(out, "Director Commentary"));
    }

    void FallsBackToLanguage()
    {
        char out[256];
        MakeTrackLabel(out, nullptr, "jpn", 2, nullptr);
        QVERIFY(::framelift::test::CStringEqual(out, "jpn"));

        MakeTrackLabel(out, "", "fre", 2, "");
        QVERIFY(::framelift::test::CStringEqual(out, "fre"));
    }

    void FallsBackToTrackOrdinal()
    {
        char out[256];
        MakeTrackLabel(out, nullptr, nullptr, 3, nullptr);
        QVERIFY(::framelift::test::CStringEqual(out, "Track 3"));
    }

    void UndeterminedLanguageFallsBackToOrdinal()
    {
        char out[256];
        MakeTrackLabel(out, nullptr, "und", 2, nullptr);
        QVERIFY(::framelift::test::CStringEqual(out, "Track 2"));

        // Case-insensitive, and only the exact 3-letter code is treated as undetermined.
        MakeTrackLabel(out, nullptr, "UND", 4, nullptr);
        QVERIFY(::framelift::test::CStringEqual(out, "Track 4"));
        MakeTrackLabel(out, nullptr, "undetermined", 5, nullptr);
        QVERIFY(::framelift::test::CStringEqual(out, "undetermined"));
    }

    void ExternalFileNameUsedWhenNoMetadata()
    {
        char out[256];
        MakeTrackLabel(out, nullptr, nullptr, 1, "Movie.en.srt");
        QVERIFY(::framelift::test::CStringEqual(out, "Movie.en.srt"));
    }

    void ExternalFileAppendedToTitle()
    {
        char out[256];
        MakeTrackLabel(out, "Forced", nullptr, 1, "Movie.forced.ass");
        QVERIFY(::framelift::test::CStringEqual(out, "Forced (Movie.forced.ass)"));
    }

    void ExternalFileAppendedToLanguage()
    {
        char out[256];
        MakeTrackLabel(out, nullptr, "eng", 1, "Movie.en.srt");
        QVERIFY(::framelift::test::CStringEqual(out, "eng (Movie.en.srt)"));
    }

    void TruncatesToBufferAndStaysTerminated()
    {
        char longTitle[400];
        std::memset(longTitle, 'A', sizeof(longTitle));
        longTitle[sizeof(longTitle) - 1] = '\0';

        char out[256];
        MakeTrackLabel(out, longTitle, nullptr, 1, nullptr);
        QVERIFY((std::strlen(out)) == (255u)); // 255 chars + NUL
        QVERIFY((out[255]) == ('\0'));
    }
};

namespace
{
const ::framelift::test::Registrar<FFmpegTrackLabelTests> kRegisterFFmpegTrackLabelTests{"FFmpegTrackLabelTests"};
}

#include "FFmpegTrackLabelTests.moc"
