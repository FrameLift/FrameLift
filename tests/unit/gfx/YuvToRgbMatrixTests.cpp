// Unit tests for YuvToRgbMatrix.h (shader YUV→RGB constants) and the
// VideoFrameDesc.h tight-layout helpers the planar upload path relies on.

#include "VideoFrameDesc.h"
#include "YuvToRgbMatrix.h"

#include "QtTestRunner.h"

#include <QtTest/QtTest>

#include <array>

namespace
{
// Apply rgb = M * yuv + bias exactly as the fragment shaders do (column-major M).
std::array<float, 3> Convert(int spc, int fullRange, int height, float y, float u, float v)
{
    float m[9];
    float bias[3];
    YuvToRgb::BuildYuvToRgbMatrix(spc, fullRange, height, m, bias);
    std::array<float, 3> rgb{};
    for (int i = 0; i < 3; ++i)
    {
        rgb[i] = m[i] * y + m[3 + i] * u + m[6 + i] * v + bias[i];
    }
    return rgb;
}

constexpr float kTol = 2.0f / 255.0f; // within two 8-bit steps of the reference
} // namespace

class YuvToRgbMatrixTests final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void LimitedRangeBlackAndWhite()
    {
        for (int spc : {YuvToRgb::kAvColSpcBt709, YuvToRgb::kAvColSpcBt470Bg, YuvToRgb::kAvColSpcBt2020Ncl})
        {
            const auto black = Convert(spc, 0, 1080, 16.0f / 255.0f, 128.0f / 255.0f, 128.0f / 255.0f);
            const auto white = Convert(spc, 0, 1080, 235.0f / 255.0f, 128.0f / 255.0f, 128.0f / 255.0f);
            for (int i = 0; i < 3; ++i)
            {
                QVERIFY(std::abs(black[i]) < kTol);
                QVERIFY(std::abs(white[i] - 1.0f) < kTol);
            }
        }
    }

    void FullRangeIsIdentityOnLuma()
    {
        const auto black = Convert(YuvToRgb::kAvColSpcBt709, 1, 1080, 0.0f, 128.0f / 255.0f, 128.0f / 255.0f);
        const auto white = Convert(YuvToRgb::kAvColSpcBt709, 1, 1080, 1.0f, 128.0f / 255.0f, 128.0f / 255.0f);
        for (int i = 0; i < 3; ++i)
        {
            QVERIFY(std::abs(black[i]) < kTol);
            QVERIFY(std::abs(white[i] - 1.0f) < kTol);
        }
    }

    void Bt601LimitedPrimaryRed()
    {
        // Classic BT.601 studio-swing red: (Y, Cb, Cr) = (81, 90, 240) → (255, 0, 0).
        const auto rgb = Convert(YuvToRgb::kAvColSpcSmpte170M, 0, 480, 81.0f / 255.0f, 90.0f / 255.0f, 240.0f / 255.0f);
        QVERIFY(std::abs(rgb[0] - 1.0f) < kTol);
        QVERIFY(std::abs(rgb[1]) < kTol);
        QVERIFY(std::abs(rgb[2]) < kTol);
    }

    void MatrixCoefficientsMatchReferences()
    {
        float m[9];
        float bias[3];
        // BT.709 limited: R←V term = 1.5748 * 255/224.
        YuvToRgb::BuildYuvToRgbMatrix(YuvToRgb::kAvColSpcBt709, 0, 1080, m, bias);
        QVERIFY(std::abs(m[6] - 1.7927f) < 1e-3f);
        QVERIFY(std::abs(m[0] - 1.16438f) < 1e-4f); // 255/219 luma expansion
        // BT.601 full: R←V term = 1.402 exactly.
        YuvToRgb::BuildYuvToRgbMatrix(YuvToRgb::kAvColSpcBt470Bg, 1, 480, m, bias);
        QVERIFY(std::abs(m[6] - 1.402f) < 1e-5f);
        QVERIFY(std::abs(m[0] - 1.0f) < 1e-6f);
    }

    void UntaggedContentResolvesByHeight()
    {
        float sd[9];
        float hd[9];
        float bias[3];
        YuvToRgb::BuildYuvToRgbMatrix(YuvToRgb::kAvColSpcUnspecified, 0, 576, sd, bias);
        YuvToRgb::BuildYuvToRgbMatrix(YuvToRgb::kAvColSpcUnspecified, 0, 720, hd, bias);
        QVERIFY(std::abs(sd[6] - 1.402f * 255.0f / 224.0f) < 1e-3f);  // 601
        QVERIFY(std::abs(hd[6] - 1.5748f * 255.0f / 224.0f) < 1e-3f); // 709
    }

    void TightLayoutNv12()
    {
        VideoFrameDesc d;
        d.format = VideoPixelFormat::NV12;
        d.w = 1920;
        d.h = 1080;
        const size_t bytes = FillTightLayout(d);
        QCOMPARE(d.stride[0], 1920);
        QCOMPARE(d.stride[1], 1920);
        QCOMPARE(d.planeOffset[1], static_cast<size_t>(1920) * 1080);
        QCOMPARE(bytes, static_cast<size_t>(1920) * 1080 * 3 / 2);
        QCOMPARE(RequiredBytes(d), bytes);
    }

    void TightLayoutYuv420pOddSize()
    {
        VideoFrameDesc d;
        d.format = VideoPixelFormat::YUV420P;
        d.w = 3;
        d.h = 3;
        const size_t bytes = FillTightLayout(d);
        QCOMPARE(d.stride[0], 3);
        QCOMPARE(d.stride[1], 2); // chroma rounds up
        QCOMPARE(d.stride[2], 2);
        QCOMPARE(d.planeOffset[1], static_cast<size_t>(9));
        QCOMPARE(d.planeOffset[2], static_cast<size_t>(13)); // 9 + 2*2
        QCOMPARE(bytes, static_cast<size_t>(17));
    }

    void TightLayoutRgba()
    {
        VideoFrameDesc d;
        d.format = VideoPixelFormat::RGBA;
        d.w = 64;
        d.h = 32;
        QCOMPARE(FillTightLayout(d), static_cast<size_t>(64) * 32 * 4);
        QCOMPARE(d.stride[0], 64 * 4);
        QCOMPARE(PlaneCount(VideoPixelFormat::RGBA), 1);
        QCOMPARE(PlaneCount(VideoPixelFormat::NV12), 2);
        QCOMPARE(PlaneCount(VideoPixelFormat::YUV420P), 3);
    }
};

namespace
{
const ::framelift::test::Registrar<YuvToRgbMatrixTests> kRegisterYuvToRgbMatrixTests{"YuvToRgbMatrixTests"};
}

#include "YuvToRgbMatrixTests.moc"
