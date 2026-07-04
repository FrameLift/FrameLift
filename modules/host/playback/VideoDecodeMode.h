#pragma once

#include "FFmpegHwDecode.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

#ifndef FRAMELIFT_MODULE_GRAPHICS_VULKAN
#define FRAMELIFT_MODULE_GRAPHICS_VULKAN 1
#endif

enum class VideoDecodeMode : int
{
    Off,
    Auto,
    VulkanZeroCopy,
    Vulkan,
    CudaZeroCopy,
    Cuda,
    D3D11VA,
    DXVA2,
    VAAPI,
};

inline std::string NormalizeDecodeModeToken(std::string_view value)
{
    std::string out(value);
    std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

inline VideoDecodeMode VideoDecodeModeFromString(std::string_view value)
{
    const std::string mode = NormalizeDecodeModeToken(value);
    if (mode == "off" || mode == "none" || mode == "software")
    {
        return VideoDecodeMode::Off;
    }
#if FRAMELIFT_MODULE_GRAPHICS_VULKAN
    if (mode == "vulkan-zero-copy" || mode == "vulkan_zero_copy" || mode == "vulkanzerocopy")
    {
        return VideoDecodeMode::VulkanZeroCopy;
    }
    if (mode == "vulkan" || mode == "vk")
    {
        return VideoDecodeMode::Vulkan;
    }
#endif
    if (mode == "cuda-zero-copy" || mode == "cuda_zero_copy" || mode == "cudazerocopy")
    {
        return VideoDecodeMode::CudaZeroCopy;
    }
    if (mode == "cuda" || mode == "nvdec" || mode == "cuvid")
    {
        return VideoDecodeMode::Cuda;
    }
    if (mode == "d3d11va")
    {
        return VideoDecodeMode::D3D11VA;
    }
    if (mode == "dxva2")
    {
        return VideoDecodeMode::DXVA2;
    }
    if (mode == "vaapi")
    {
        return VideoDecodeMode::VAAPI;
    }
    return VideoDecodeMode::Auto;
}

inline const char* VideoDecodeModeName(VideoDecodeMode mode)
{
    switch (mode)
    {
    case VideoDecodeMode::Off:
        return "off";
    case VideoDecodeMode::VulkanZeroCopy:
#if FRAMELIFT_MODULE_GRAPHICS_VULKAN
        return "vulkan-zero-copy";
#else
        break;
#endif
    case VideoDecodeMode::Vulkan:
#if FRAMELIFT_MODULE_GRAPHICS_VULKAN
        return "vulkan";
#else
        break;
#endif
    case VideoDecodeMode::CudaZeroCopy:
        return "cuda-zero-copy";
    case VideoDecodeMode::Cuda:
        return "cuda";
    case VideoDecodeMode::D3D11VA:
        return "d3d11va";
    case VideoDecodeMode::DXVA2:
        return "dxva2";
    case VideoDecodeMode::VAAPI:
        return "vaapi";
    case VideoDecodeMode::Auto:
        break;
    }
    return "auto";
}

inline bool IsVideoDecodeModeEnabled(VideoDecodeMode mode)
{
    return mode != VideoDecodeMode::Off;
}

inline HwBackend HwBackendFromVideoDecodeMode(VideoDecodeMode mode)
{
    switch (mode)
    {
    case VideoDecodeMode::Vulkan:
#if FRAMELIFT_MODULE_GRAPHICS_VULKAN
        return HwBackend::Vulkan;
#else
        break;
#endif
    case VideoDecodeMode::Cuda:
        return HwBackend::Cuda;
    case VideoDecodeMode::D3D11VA:
        return HwBackend::D3D11VA;
    case VideoDecodeMode::DXVA2:
        return HwBackend::DXVA2;
    case VideoDecodeMode::VAAPI:
        return HwBackend::VAAPI;
    case VideoDecodeMode::Off:
    case VideoDecodeMode::Auto:
    case VideoDecodeMode::VulkanZeroCopy:
    case VideoDecodeMode::CudaZeroCopy:
        break;
    }
    return HwBackend::None;
}

inline std::array<VideoDecodeMode, 6> AutoVideoDecodePreference()
{
#if defined(_WIN32)
#if FRAMELIFT_MODULE_GRAPHICS_VULKAN
    return {VideoDecodeMode::VulkanZeroCopy, VideoDecodeMode::CudaZeroCopy, VideoDecodeMode::Cuda,
            VideoDecodeMode::D3D11VA,        VideoDecodeMode::DXVA2,        VideoDecodeMode::Off};
#else
    return {VideoDecodeMode::CudaZeroCopy, VideoDecodeMode::Cuda,  VideoDecodeMode::D3D11VA,
            VideoDecodeMode::DXVA2,        VideoDecodeMode::Off,   VideoDecodeMode::Off};
#endif
#else
#if FRAMELIFT_MODULE_GRAPHICS_VULKAN
    return {VideoDecodeMode::VulkanZeroCopy, VideoDecodeMode::CudaZeroCopy, VideoDecodeMode::Cuda,
            VideoDecodeMode::VAAPI,          VideoDecodeMode::Off,          VideoDecodeMode::Off};
#else
    return {VideoDecodeMode::CudaZeroCopy, VideoDecodeMode::Cuda, VideoDecodeMode::VAAPI,
            VideoDecodeMode::Off,          VideoDecodeMode::Off,  VideoDecodeMode::Off};
#endif
#endif
}

// Every user-selectable decode mode worth *offering* on this platform/build, in
// menu order: Off and Auto first, then the hardware modes that could exist here.
// Gated by the same _WIN32 / Vulkan-module partitioning as AutoVideoDecodePreference
// so it never lists a cross-platform mode (no d3d11va on Linux, no vaapi on Windows).
// Machine-level availability (does the GPU/driver actually support the backend) is a
// separate runtime probe — see ProbeHwBackendAvailable + HwBackendForProbe. Pure /
// libav-free so the native test suite can exercise it.
inline std::vector<VideoDecodeMode> CandidateVideoDecodeModes()
{
    std::vector<VideoDecodeMode> modes = {VideoDecodeMode::Off, VideoDecodeMode::Auto};
#if FRAMELIFT_MODULE_GRAPHICS_VULKAN
    modes.push_back(VideoDecodeMode::VulkanZeroCopy);
    modes.push_back(VideoDecodeMode::Vulkan);
#endif
    modes.push_back(VideoDecodeMode::CudaZeroCopy);
    modes.push_back(VideoDecodeMode::Cuda);
#if defined(_WIN32)
    modes.push_back(VideoDecodeMode::D3D11VA);
    modes.push_back(VideoDecodeMode::DXVA2);
#else
    modes.push_back(VideoDecodeMode::VAAPI);
#endif
    return modes;
}

// The hardware device whose availability gates a mode. Unlike
// HwBackendFromVideoDecodeMode (which is about arming the readback path and returns
// None for the zero-copy variants), this maps every hardware mode — including the
// zero-copy variants — to the base backend it needs, so a single av_hwdevice probe
// decides whether the mode can be offered. Off/Auto (and, without the Vulkan module,
// the Vulkan modes) return None.
inline HwBackend HwBackendForProbe(VideoDecodeMode mode)
{
    switch (mode)
    {
    case VideoDecodeMode::VulkanZeroCopy:
    case VideoDecodeMode::Vulkan:
#if FRAMELIFT_MODULE_GRAPHICS_VULKAN
        return HwBackend::Vulkan;
#else
        break;
#endif
    case VideoDecodeMode::CudaZeroCopy:
    case VideoDecodeMode::Cuda:
        return HwBackend::Cuda;
    case VideoDecodeMode::D3D11VA:
        return HwBackend::D3D11VA;
    case VideoDecodeMode::DXVA2:
        return HwBackend::DXVA2;
    case VideoDecodeMode::VAAPI:
        return HwBackend::VAAPI;
    case VideoDecodeMode::Off:
    case VideoDecodeMode::Auto:
        break;
    }
    return HwBackend::None;
}

// True when `value` is a token VideoDecodeModeFromString recognizes (any alias),
// rather than silently collapsing to Auto. Used to reject a bogus FL_ACCEL_MODE
// before it looks like a valid "auto" request. Mirrors the same aliases and the
// same Vulkan-module gating as VideoDecodeModeFromString.
inline bool IsKnownDecodeModeToken(std::string_view value)
{
    const std::string mode = NormalizeDecodeModeToken(value);
    if (mode == "off" || mode == "none" || mode == "software" || mode == "auto")
    {
        return true;
    }
#if FRAMELIFT_MODULE_GRAPHICS_VULKAN
    if (mode == "vulkan-zero-copy" || mode == "vulkan_zero_copy" || mode == "vulkanzerocopy" ||
        mode == "vulkan" || mode == "vk")
    {
        return true;
    }
#endif
    if (mode == "cuda-zero-copy" || mode == "cuda_zero_copy" || mode == "cudazerocopy" || mode == "cuda" ||
        mode == "nvdec" || mode == "cuvid")
    {
        return true;
    }
    return mode == "d3d11va" || mode == "dxva2" || mode == "vaapi";
}
