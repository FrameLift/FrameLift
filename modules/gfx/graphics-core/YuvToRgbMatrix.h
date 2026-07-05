#pragma once

// Builds the YUV→RGB conversion the video shaders apply to planar (NV12/I420)
// frames sampled as UNORM 0..1: rgb = M * yuv + bias, one multiply-add. The
// limited-range expansion (16..235 / 16..240) and the −128 chroma offset are
// folded into M/bias, so the shader needs no other constants.
//
// Deliberately libav-free: colorspace/range arrive as the numeric AV ids the
// decoder tags frames with (same convention as SwsColorspace.h /
// VulkanColorMapping.h), so this header is unit-testable without FFmpeg.
namespace YuvToRgb
{
inline constexpr int kAvColSpcBt709 = 1;
inline constexpr int kAvColSpcUnspecified = 2;
inline constexpr int kAvColSpcFcc = 4;
inline constexpr int kAvColSpcBt470Bg = 5;   // BT.601-625
inline constexpr int kAvColSpcSmpte170M = 6; // BT.601-525
inline constexpr int kAvColSpcBt2020Ncl = 9;
inline constexpr int kAvColSpcBt2020Cl = 10;

// Column-major 3x3 (GL uniform convention: outMat[c*3 + r]) plus bias, so that
//   rgb = M * (y, u, v) + bias
// with y/u/v sampled as 0..1. `height` drives the untagged-content fallback:
// SD (<= 576) → BT.601, HD and up → BT.709 (the SwsColorspace.h convention).
// fullRange: 1 = full/JPEG range, 0 = limited/MPEG.
constexpr void BuildYuvToRgbMatrix(int avColorSpace, int fullRange, int height, float outMat[9], float outBias[3])
{
    // K coefficients per matrix: R = Y' + a·Cr, G = Y' + b·Cb + c·Cr, B = Y' + d·Cb,
    // derived from (Kr, Kb) via a = 2(1−Kr), d = 2(1−Kb), b = −d·Kb/Kg, c = −a·Kr/Kg.
    double a = 1.5748, b = -0.18732427, c = -0.46812427, d = 1.8556; // BT.709 (Kr .2126, Kb .0722)
    int spc = avColorSpace;
    if (spc == kAvColSpcUnspecified || spc < 0)
    {
        spc = height > 576 ? kAvColSpcBt709 : kAvColSpcBt470Bg;
    }
    if (spc == kAvColSpcBt470Bg || spc == kAvColSpcSmpte170M || spc == kAvColSpcFcc)
    {
        a = 1.402, b = -0.34413629, c = -0.71413629, d = 1.772; // BT.601 (Kr .299, Kb .114)
    }
    else if (spc == kAvColSpcBt2020Ncl || spc == kAvColSpcBt2020Cl)
    {
        a = 1.4746, b = -0.16455313, c = -0.57135313, d = 1.8814; // BT.2020 (Kr .2627, Kb .0593)
    }

    // Range expansion in the 0..1 sampled domain: Y' = sy·(Y − oy), C = sc·(U|V − 128/255).
    const double sy = fullRange ? 1.0 : 255.0 / 219.0;
    const double sc = fullRange ? 1.0 : 255.0 / 224.0;
    const double oy = fullRange ? 0.0 : 16.0 / 255.0;
    const double oc = 128.0 / 255.0;

    // M = K · diag(sy, sc, sc), column-major.
    outMat[0] = static_cast<float>(sy);     // R ← Y
    outMat[1] = static_cast<float>(sy);     // G ← Y
    outMat[2] = static_cast<float>(sy);     // B ← Y
    outMat[3] = 0.0f;                       // R ← U
    outMat[4] = static_cast<float>(b * sc); // G ← U
    outMat[5] = static_cast<float>(d * sc); // B ← U
    outMat[6] = static_cast<float>(a * sc); // R ← V
    outMat[7] = static_cast<float>(c * sc); // G ← V
    outMat[8] = 0.0f;                       // B ← V

    // bias = −K · (sy·oy, sc·oc, sc·oc).
    const double yb = sy * oy;
    const double cb = sc * oc;
    outBias[0] = static_cast<float>(-(yb + a * cb));
    outBias[1] = static_cast<float>(-(yb + (b + c) * cb));
    outBias[2] = static_cast<float>(-(yb + d * cb));
}
} // namespace YuvToRgb
