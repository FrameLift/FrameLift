#include "FFmpegFilters.h"

#include <gtest/gtest.h>

TEST(FFmpegFiltersTest, DefaultParamsProduceExpectedChain)
{
    const std::string f = BuildAudioNormalizeGraph(AudioNormalizeParams{});
    // The libavfilter chain description, without any "lavfi=[...]" wrapper
    // (abuffer/abuffersink are added when the graph is built).
    EXPECT_EQ(f, "dynaudnorm=f=100:g=5:p=0.950000:m=5.000000"
                 ",asoftclip=type=tanh,volume=1.500000");
}

TEST(FFmpegFiltersTest, GaussSizeIsForcedOdd)
{
    AudioNormalizeParams p;
    p.gaussSize = 4;
    EXPECT_NE(BuildAudioNormalizeGraph(p).find(":g=5"), std::string::npos);
    p.gaussSize = 6;
    EXPECT_NE(BuildAudioNormalizeGraph(p).find(":g=7"), std::string::npos);
    p.gaussSize = 5; // already odd — unchanged
    EXPECT_NE(BuildAudioNormalizeGraph(p).find(":g=5"), std::string::npos);
}

TEST(FFmpegFiltersTest, FrameLengthIsReflected)
{
    AudioNormalizeParams p;
    p.frameLen = 250;
    EXPECT_NE(BuildAudioNormalizeGraph(p).find("dynaudnorm=f=250"), std::string::npos);
}

TEST(FFmpegFiltersTest, AlwaysIncludesSoftClipSafetyNet)
{
    EXPECT_NE(BuildAudioNormalizeGraph(AudioNormalizeParams{}).find("asoftclip=type=tanh"), std::string::npos);
}

TEST(FFmpegFiltersTest, NoLavfiWrapper)
{
    // The FFmpeg backend builds an avfilter graph directly; no "lavfi=[...]" wrapper is used.
    const std::string f = BuildAudioNormalizeGraph(AudioNormalizeParams{});
    EXPECT_EQ(f.find("lavfi"), std::string::npos);
}
