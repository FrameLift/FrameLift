#pragma once

// FFmpeg AVColorSpace / AVColorRange → libswscale color-conversion parameters.
// FFmpeg-free on purpose (the VulkanColorMapping.h precedent) so the resolution
// logic is unit-testable in the FFmpeg-less test build. The values are the numeric
// ISO/ITU ids that both AVColorSpace and sws_getCoefficients() accept (a stable
// FFmpeg ABI); the caller feeds ResolveCoefficients()'s result straight into
// sws_getCoefficients() and FullRange()'s result into the swscale src_range flag.
namespace SwsColorspace
{
inline constexpr int kAvColSpcRgb = 0;         // AVCOL_SPC_RGB
inline constexpr int kAvColSpcBt709 = 1;       // AVCOL_SPC_BT709 (== SWS_CS_ITU709)
inline constexpr int kAvColSpcUnspecified = 2; // AVCOL_SPC_UNSPECIFIED
inline constexpr int kAvColSpc601 = 5;         // AVCOL_SPC_BT470BG (== SWS_CS_ITU601 / DEFAULT)
inline constexpr int kAvColRangeJpeg = 2;      // AVCOL_RANGE_JPEG (full range)

// Pick the YUV→RGB coefficient id to hand sws_getCoefficients(). When the frame
// carries no usable colorspace tag (unspecified, RGB, or a negative/garbage value)
// fall back by resolution — SD (height <= 576) → BT.601, HD and up → BT.709 — the
// convention decoders themselves use. A tagged value passes through unchanged;
// values sws_getCoefficients() doesn't model (e.g. BT.2020) it clamps to its own
// default internally.
constexpr int ResolveCoefficients(int avColorSpace, int height)
{
    if (avColorSpace == kAvColSpcUnspecified || avColorSpace == kAvColSpcRgb || avColorSpace < 0)
    {
        return height > 576 ? kAvColSpcBt709 : kAvColSpc601;
    }
    return avColorSpace;
}

// AVColorRange → swscale src_range flag: 1 (full) only for JPEG/full-range content,
// else 0 (limited/MPEG, the safe default when the range is unspecified).
constexpr int FullRange(int avColorRange)
{
    return avColorRange == kAvColRangeJpeg ? 1 : 0;
}
} // namespace SwsColorspace
