// Unit tests for the FFmpeg backend's libass overlay compositing (issue #8, Phase 5).
// FFmpegSubtitleBlend.h is free of <ass/ass.h> so it builds in the native suite.

#include "FFmpegSubtitleBlend.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

namespace
{
// White, fully opaque (AA transparency byte == 0).
constexpr uint32_t kWhiteOpaque = 0xFFFFFF00u;
} // namespace

TEST(FFmpegSubtitleBlendTests, FullCoverageOpaqueWritesSolidColor)
{
    uint8_t dst[4] = {0, 0, 0, 0};
    BlendOverPixel(dst, 10, 20, 30, 255);
    EXPECT_EQ(dst[0], 10);
    EXPECT_EQ(dst[1], 20);
    EXPECT_EQ(dst[2], 30);
    EXPECT_EQ(dst[3], 255);
}

TEST(FFmpegSubtitleBlendTests, ZeroAlphaLeavesDestinationUntouched)
{
    uint8_t dst[4] = {7, 8, 9, 200};
    BlendOverPixel(dst, 255, 255, 255, 0);
    EXPECT_EQ(dst[0], 7);
    EXPECT_EQ(dst[1], 8);
    EXPECT_EQ(dst[2], 9);
    EXPECT_EQ(dst[3], 200);
}

TEST(FFmpegSubtitleBlendTests, HalfAlphaBlendsOverOpaqueBackground)
{
    uint8_t dst[4] = {0, 0, 0, 255};
    // a = 128: white over black ≈ 128.
    BlendOverPixel(dst, 255, 255, 255, 128);
    EXPECT_NEAR(dst[0], 128, 1);
    EXPECT_NEAR(dst[1], 128, 1);
    EXPECT_NEAR(dst[2], 128, 1);
    EXPECT_EQ(dst[3], 255); // a + 255*(255-a)/255 == 255
}

TEST(FFmpegSubtitleBlendTests, FullCoverageFullyOpaqueColorFillsRegion)
{
    std::array<uint8_t, 2 * 2 * 4> frame{}; // 2x2, zero-initialised (transparent)
    const std::array<uint8_t, 2 * 2> cov = {255, 255, 255, 255};
    BlendCoverageBitmap(frame.data(), 2, 2, cov.data(), 2, 2, /*srcStride*/ 2, /*dstX*/ 0, /*dstY*/ 0, kWhiteOpaque);
    for (int px = 0; px < 4; ++px)
    {
        EXPECT_EQ(frame[px * 4 + 0], 255);
        EXPECT_EQ(frame[px * 4 + 1], 255);
        EXPECT_EQ(frame[px * 4 + 2], 255);
        EXPECT_EQ(frame[px * 4 + 3], 255);
    }
}

TEST(FFmpegSubtitleBlendTests, TransparentColorByteIsNoOp)
{
    std::array<uint8_t, 4> frame{};
    const std::array<uint8_t, 1> cov = {255};
    // AA == 255 → fully transparent, nothing drawn.
    BlendCoverageBitmap(frame.data(), 1, 1, cov.data(), 1, 1, 1, 0, 0, 0xFFFFFFFFu);
    EXPECT_EQ(frame[3], 0);
}

TEST(FFmpegSubtitleBlendTests, CoverageScalesAlpha)
{
    std::array<uint8_t, 4> frame{}; // transparent over black-ish base
    const std::array<uint8_t, 1> cov = {128}; // half coverage
    BlendCoverageBitmap(frame.data(), 1, 1, cov.data(), 1, 1, 1, 0, 0, kWhiteOpaque);
    // a = 128 * 255/255 = 128; over transparent dst (0): rgb stays scaled, alpha ~128.
    EXPECT_NEAR(frame[3], 128, 1);
}

TEST(FFmpegSubtitleBlendTests, ClipsNegativeAndOverflowingPlacement)
{
    std::array<uint8_t, 2 * 2 * 4> frame{};
    const std::array<uint8_t, 2 * 2> cov = {255, 255, 255, 255};
    // Place a 2x2 bitmap at (-1,-1): only its bottom-right pixel lands at (0,0).
    BlendCoverageBitmap(frame.data(), 2, 2, cov.data(), 2, 2, 2, -1, -1, kWhiteOpaque);
    EXPECT_EQ(frame[0 * 4 + 3], 255); // (0,0) written
    EXPECT_EQ(frame[1 * 4 + 3], 0);   // (1,0) untouched
    EXPECT_EQ(frame[2 * 4 + 3], 0);   // (0,1) untouched
    EXPECT_EQ(frame[3 * 4 + 3], 0);   // (1,1) untouched
}
