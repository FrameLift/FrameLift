#pragma once

#include <cstddef>
#include <cstdint>

// Pure CPU compositing of libass-rendered glyph bitmaps onto an RGBA8 overlay
// buffer (issue #8, Phase 5). Deliberately free of <ass/ass.h> so it builds in the
// standalone native test suite; FFmpegSubtitles.cpp translates each ASS_Image's
// fields into the parameters below.
//
// libass gives, per image: an 8-bit coverage bitmap (row stride srcStride, width w,
// height h) positioned at (dstX,dstY) in the frame, plus a single packed colour
// 0xRRGGBBAA where the LOW byte AA is *transparency* (0 = fully opaque, 255 = fully
// transparent). Effective per-pixel source alpha is
//   a = coverage * (255 - AA) / 255
// and pixels are straight-alpha "over"-composited onto the (zero-initialised) frame.

// Blend a single straight-alpha source pixel over an RGBA8 destination pixel.
inline void BlendOverPixel(uint8_t* dst, uint8_t r, uint8_t g, uint8_t b, int a)
{
    if (a <= 0)
    {
        return;
    }
    if (a >= 255)
    {
        dst[0] = r;
        dst[1] = g;
        dst[2] = b;
        dst[3] = 255;
        return;
    }
    const int ia = 255 - a;
    dst[0] = static_cast<uint8_t>((r * a + dst[0] * ia) / 255);
    dst[1] = static_cast<uint8_t>((g * a + dst[1] * ia) / 255);
    dst[2] = static_cast<uint8_t>((b * a + dst[2] * ia) / 255);
    dst[3] = static_cast<uint8_t>(a + dst[3] * ia / 255);
}

// Composite one coverage bitmap over an RGBA8 frame of size frameW×frameH. color is
// 0xRRGGBBAA (AA = transparency). The bitmap may be placed partly off-screen via
// negative or large dstX/dstY; the blit is clipped to the frame bounds.
inline void BlendCoverageBitmap(uint8_t* frame, int frameW, int frameH, const uint8_t* bitmap, int w, int h,
                                int srcStride, int dstX, int dstY, uint32_t color)
{
    if (!frame || !bitmap || w <= 0 || h <= 0 || frameW <= 0 || frameH <= 0)
    {
        return;
    }
    const auto r = static_cast<uint8_t>((color >> 24) & 0xFFu);
    const auto g = static_cast<uint8_t>((color >> 16) & 0xFFu);
    const auto b = static_cast<uint8_t>((color >> 8) & 0xFFu);
    const int opacity = 255 - static_cast<int>(color & 0xFFu); // AA byte is transparency
    if (opacity <= 0)
    {
        return;
    }

    for (int sy = 0; sy < h; ++sy)
    {
        const int fy = dstY + sy;
        if (fy < 0 || fy >= frameH)
        {
            continue;
        }
        const uint8_t* srcRow = bitmap + static_cast<std::size_t>(sy) * static_cast<std::size_t>(srcStride);
        uint8_t* dstRow = frame + static_cast<std::size_t>(fy) * static_cast<std::size_t>(frameW) * 4;
        for (int sx = 0; sx < w; ++sx)
        {
            const int fx = dstX + sx;
            if (fx < 0 || fx >= frameW)
            {
                continue;
            }
            const int cov = srcRow[sx];
            if (cov == 0)
            {
                continue;
            }
            const int a = cov * opacity / 255;
            BlendOverPixel(dstRow + static_cast<std::size_t>(fx) * 4, r, g, b, a);
        }
    }
}
