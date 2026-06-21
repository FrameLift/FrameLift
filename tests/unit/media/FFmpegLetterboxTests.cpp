// Unit tests for the FFmpeg backend's letterbox/pillarbox math (issue #8, Phase 5).
// FFmpegLetterbox.h is free of libav/GL so it builds in the standalone native suite.

#include "FFmpegLetterbox.h"

#include <gtest/gtest.h>

TEST(FFmpegLetterboxTests, ExactAspectFillsFramebuffer)
{
    const LetterboxRect r = ComputeLetterbox(1920, 1080, 1920, 1080);
    EXPECT_EQ(r.x, 0);
    EXPECT_EQ(r.y, 0);
    EXPECT_EQ(r.w, 1920);
    EXPECT_EQ(r.h, 1080);
}

TEST(FFmpegLetterboxTests, SameAspectDifferentScaleFills)
{
    // 16:9 video into a 16:9 framebuffer of a different size — no bars.
    const LetterboxRect r = ComputeLetterbox(1280, 720, 1920, 1080);
    EXPECT_EQ(r.x, 0);
    EXPECT_EQ(r.y, 0);
    EXPECT_EQ(r.w, 1280);
    EXPECT_EQ(r.h, 720);
}

TEST(FFmpegLetterboxTests, WideFramebufferPillarboxes)
{
    // 4:3 video in a 16:9 window → bars left/right, full height.
    const LetterboxRect r = ComputeLetterbox(1600, 900, 640, 480);
    EXPECT_EQ(r.h, 900);
    EXPECT_EQ(r.w, 1200); // 900 * 4/3
    EXPECT_EQ(r.x, 200);  // (1600 - 1200) / 2
    EXPECT_EQ(r.y, 0);
}

TEST(FFmpegLetterboxTests, TallFramebufferLetterboxes)
{
    // 16:9 video in a 4:3 window → bars top/bottom, full width.
    const LetterboxRect r = ComputeLetterbox(800, 600, 1920, 1080);
    EXPECT_EQ(r.w, 800);
    EXPECT_EQ(r.h, 450); // 800 * 9/16
    EXPECT_EQ(r.x, 0);
    EXPECT_EQ(r.y, 75); // (600 - 450) / 2
}

TEST(FFmpegLetterboxTests, RoundsToNearestPixel)
{
    // 1:1 video into 101x100 → width rounds 100*1 = 100, centered with 1px bar.
    const LetterboxRect r = ComputeLetterbox(101, 100, 100, 100);
    EXPECT_EQ(r.w, 100);
    EXPECT_EQ(r.h, 100);
    EXPECT_EQ(r.x, 0); // (101 - 100) / 2 == 0 (integer)
}

TEST(FFmpegLetterboxTests, DegenerateInputsClampToFramebuffer)
{
    const LetterboxRect a = ComputeLetterbox(640, 480, 0, 0);
    EXPECT_EQ(a.w, 640);
    EXPECT_EQ(a.h, 480);

    const LetterboxRect b = ComputeLetterbox(0, 0, 1920, 1080);
    EXPECT_EQ(b.w, 0);
    EXPECT_EQ(b.h, 0);
}
