// Unit tests for the FFmpeg backend's track-label builder (issue #8, Phase 5).
// FFmpegTrackLabel.h is free of libav so it builds in the standalone native suite.

#include "FFmpegTrackLabel.h"

#include <gtest/gtest.h>

#include <cstring>

TEST(FFmpegTrackLabelTests, PrefersTitle)
{
    char out[256];
    MakeTrackLabel(out, "Director Commentary", "eng", 1, nullptr);
    EXPECT_STREQ(out, "Director Commentary");
}

TEST(FFmpegTrackLabelTests, FallsBackToLanguage)
{
    char out[256];
    MakeTrackLabel(out, nullptr, "jpn", 2, nullptr);
    EXPECT_STREQ(out, "jpn");

    MakeTrackLabel(out, "", "fre", 2, "");
    EXPECT_STREQ(out, "fre");
}

TEST(FFmpegTrackLabelTests, FallsBackToTrackOrdinal)
{
    char out[256];
    MakeTrackLabel(out, nullptr, nullptr, 3, nullptr);
    EXPECT_STREQ(out, "Track 3");
}

TEST(FFmpegTrackLabelTests, ExternalFileNameUsedWhenNoMetadata)
{
    char out[256];
    MakeTrackLabel(out, nullptr, nullptr, 1, "Movie.en.srt");
    EXPECT_STREQ(out, "Movie.en.srt");
}

TEST(FFmpegTrackLabelTests, ExternalFileAppendedToTitle)
{
    char out[256];
    MakeTrackLabel(out, "Forced", nullptr, 1, "Movie.forced.ass");
    EXPECT_STREQ(out, "Forced (Movie.forced.ass)");
}

TEST(FFmpegTrackLabelTests, ExternalFileAppendedToLanguage)
{
    char out[256];
    MakeTrackLabel(out, nullptr, "eng", 1, "Movie.en.srt");
    EXPECT_STREQ(out, "eng (Movie.en.srt)");
}

TEST(FFmpegTrackLabelTests, TruncatesToBufferAndStaysTerminated)
{
    char longTitle[400];
    std::memset(longTitle, 'A', sizeof(longTitle));
    longTitle[sizeof(longTitle) - 1] = '\0';

    char out[256];
    MakeTrackLabel(out, longTitle, nullptr, 1, nullptr);
    EXPECT_EQ(std::strlen(out), 255u); // 255 chars + NUL
    EXPECT_EQ(out[255], '\0');
}
