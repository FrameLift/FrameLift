// Unit tests for the FFmpeg backend's libass overlay compositing (issue #8, Phase 5).
// FFmpegSubtitleBlend.h is free of <ass/ass.h> so it builds in the native suite.

#include "FFmpegSubtitleBlend.h"

#include "QtTestRunner.h"

#include <QtTest/QtTest>

#include <array>
#include <cstdint>

namespace
{
// White, fully opaque (AA transparency byte == 0).
constexpr uint32_t kWhiteOpaque = 0xFFFFFF00u;
} // namespace

class FFmpegSubtitleBlendTests final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void FullCoverageOpaqueWritesSolidColor()
    {
        uint8_t dst[4] = {0, 0, 0, 0};
        BlendOverPixel(dst, 10, 20, 30, 255);
        QVERIFY((dst[0]) == (10));
        QVERIFY((dst[1]) == (20));
        QVERIFY((dst[2]) == (30));
        QVERIFY((dst[3]) == (255));
    }

    void ZeroAlphaLeavesDestinationUntouched()
    {
        uint8_t dst[4] = {7, 8, 9, 200};
        BlendOverPixel(dst, 255, 255, 255, 0);
        QVERIFY((dst[0]) == (7));
        QVERIFY((dst[1]) == (8));
        QVERIFY((dst[2]) == (9));
        QVERIFY((dst[3]) == (200));
    }

    void HalfAlphaBlendsOverOpaqueBackground()
    {
        uint8_t dst[4] = {0, 0, 0, 255};
        // a = 128: white over black ≈ 128.
        BlendOverPixel(dst, 255, 255, 255, 128);
        QVERIFY(::framelift::test::NearlyEqual(dst[0], 128, 1));
        QVERIFY(::framelift::test::NearlyEqual(dst[1], 128, 1));
        QVERIFY(::framelift::test::NearlyEqual(dst[2], 128, 1));
        QVERIFY((dst[3]) == (255)); // a + 255*(255-a)/255 == 255
    }

    void FullCoverageFullyOpaqueColorFillsRegion()
    {
        std::array<uint8_t, 2 * 2 * 4> frame{}; // 2x2, zero-initialised (transparent)
        const std::array<uint8_t, 2 * 2> cov = {255, 255, 255, 255};
        BlendCoverageBitmap(
            frame.data(), 2, 2, cov.data(), 2, 2, /*srcStride*/ 2, /*dstX*/ 0, /*dstY*/ 0, kWhiteOpaque
        );
        for (int px = 0; px < 4; ++px)
        {
            QVERIFY((frame[px * 4 + 0]) == (255));
            QVERIFY((frame[px * 4 + 1]) == (255));
            QVERIFY((frame[px * 4 + 2]) == (255));
            QVERIFY((frame[px * 4 + 3]) == (255));
        }
    }

    void TransparentColorByteIsNoOp()
    {
        std::array<uint8_t, 4> frame{};
        const std::array<uint8_t, 1> cov = {255};
        // AA == 255 → fully transparent, nothing drawn.
        BlendCoverageBitmap(frame.data(), 1, 1, cov.data(), 1, 1, 1, 0, 0, 0xFFFFFFFFu);
        QVERIFY((frame[3]) == (0));
    }

    void CoverageScalesAlpha()
    {
        std::array<uint8_t, 4> frame{};           // transparent over black-ish base
        const std::array<uint8_t, 1> cov = {128}; // half coverage
        BlendCoverageBitmap(frame.data(), 1, 1, cov.data(), 1, 1, 1, 0, 0, kWhiteOpaque);
        // a = 128 * 255/255 = 128; over transparent dst (0): rgb stays scaled, alpha ~128.
        QVERIFY(::framelift::test::NearlyEqual(frame[3], 128, 1));
    }

    void ClipsNegativeAndOverflowingPlacement()
    {
        std::array<uint8_t, 2 * 2 * 4> frame{};
        const std::array<uint8_t, 2 * 2> cov = {255, 255, 255, 255};
        // Place a 2x2 bitmap at (-1,-1): only its bottom-right pixel lands at (0,0).
        BlendCoverageBitmap(frame.data(), 2, 2, cov.data(), 2, 2, 2, -1, -1, kWhiteOpaque);
        QVERIFY((frame[0 * 4 + 3]) == (255)); // (0,0) written
        QVERIFY((frame[1 * 4 + 3]) == (0));   // (1,0) untouched
        QVERIFY((frame[2 * 4 + 3]) == (0));   // (0,1) untouched
        QVERIFY((frame[3 * 4 + 3]) == (0));   // (1,1) untouched
    }
};

namespace
{
const ::framelift::test::Registrar<FFmpegSubtitleBlendTests> kRegisterFFmpegSubtitleBlendTests{
    "FFmpegSubtitleBlendTests"
};
}

#include "FFmpegSubtitleBlendTests.moc"
