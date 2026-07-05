#pragma once

#include <cstddef>
#include <cstdint>

// Describes one CPU-side video frame handed from the decode worker to the video
// renderer. Deliberately libav/GL/Vulkan-free (VulkanColorMapping.h precedent) so
// the layout helpers are unit-testable in the FFmpeg-less test build and the header
// is includable from both the playback module and the gfx backends.
//
// The pixel data itself travels as one contiguous, tightly packed buffer; planes
// are addressed via planeOffset[]/stride[]. Formats:
//   RGBA    — 1 plane,  w*4 bytes/row, h rows.
//   NV12    — 2 planes: Y (w × h, 1 B/px) + interleaved UV (w × h/2 — chroma pairs,
//             so w bytes/row over h/2 rows).
//   YUV420P — 3 planes: Y (w × h) + U (w/2 × h/2) + V (w/2 × h/2).
enum class VideoPixelFormat : uint8_t
{
    RGBA,
    NV12,
    YUV420P,
};

struct VideoFrameDesc
{
    VideoPixelFormat format = VideoPixelFormat::RGBA;
    int w = 0; // full-frame luma dimensions
    int h = 0;
    int stride[3] = {0, 0, 0};         // bytes per row, per plane
    size_t planeOffset[3] = {0, 0, 0}; // byte offset of each plane in the buffer
    int colorspace = 2;                // AV numeric colorspace id (2 = unspecified; SwsColorspace.h)
    int fullRange = 0;                 // 1 = full/JPEG range, 0 = limited/MPEG
};

constexpr int PlaneCount(VideoPixelFormat f)
{
    switch (f)
    {
    case VideoPixelFormat::RGBA:
        return 1;
    case VideoPixelFormat::NV12:
        return 2;
    case VideoPixelFormat::YUV420P:
        return 3;
    }
    return 0;
}

// Rows of plane `i` for a frame of luma height h (chroma planes are half height for
// both YUV formats here; 4:2:0 only).
constexpr int PlaneRows(VideoPixelFormat f, int i, int h)
{
    return (f == VideoPixelFormat::RGBA || i == 0) ? h : (h + 1) / 2;
}

// Fill stride/planeOffset for a tightly packed frame of the given format/size and
// return the total byte size the pixel buffer needs.
constexpr size_t FillTightLayout(VideoFrameDesc& d)
{
    const int cw = (d.w + 1) / 2; // chroma width in samples
    switch (d.format)
    {
    case VideoPixelFormat::RGBA:
        d.stride[0] = d.w * 4;
        d.stride[1] = d.stride[2] = 0;
        d.planeOffset[0] = d.planeOffset[1] = d.planeOffset[2] = 0;
        break;
    case VideoPixelFormat::NV12:
        d.stride[0] = d.w;
        d.stride[1] = cw * 2; // interleaved UV pairs
        d.stride[2] = 0;
        d.planeOffset[0] = 0;
        d.planeOffset[1] = static_cast<size_t>(d.stride[0]) * d.h;
        d.planeOffset[2] = 0;
        break;
    case VideoPixelFormat::YUV420P:
        d.stride[0] = d.w;
        d.stride[1] = cw;
        d.stride[2] = cw;
        d.planeOffset[0] = 0;
        d.planeOffset[1] = static_cast<size_t>(d.stride[0]) * d.h;
        d.planeOffset[2] = d.planeOffset[1] + static_cast<size_t>(d.stride[1]) * PlaneRows(d.format, 1, d.h);
        break;
    }
    const int last = PlaneCount(d.format) - 1;
    return d.planeOffset[last] + static_cast<size_t>(d.stride[last]) * PlaneRows(d.format, last, d.h);
}

constexpr size_t RequiredBytes(const VideoFrameDesc& d)
{
    const int last = PlaneCount(d.format) - 1;
    return d.planeOffset[last] + static_cast<size_t>(d.stride[last]) * PlaneRows(d.format, last, d.h);
}
