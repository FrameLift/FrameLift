#pragma once

#include "SettingsRegistry.h"
#include "VideoDecodeMode.h"

#include <string>

// Playback settings — owned by the media/ffmpeg module.

#ifndef FRAMELIFT_MODULE_GRAPHICS_VULKAN
#define FRAMELIFT_MODULE_GRAPHICS_VULKAN 1
#endif

#if FRAMELIFT_MODULE_GRAPHICS_VULKAN
#define FRAMELIFT_HWDEC_MODE_DESC \
    "Video acceleration mode: off, auto, vulkan-zero-copy, vulkan, cuda-zero-copy, cuda, d3d11va, dxva2, or vaapi."
#else
#define FRAMELIFT_HWDEC_MODE_DESC "Video acceleration mode: off, auto, cuda-zero-copy, cuda, d3d11va, dxva2, or vaapi."
#endif

struct PlaybackSettings
{
    bool hwdec = true;
    std::string hwdecMode = "auto";
    bool hrSeek = true;
    bool subAutoLoad = true;
    bool audioFileAutoLoad = true;
};

inline void RegisterPlaybackSettings(SettingsRegistry& reg, PlaybackSettings& s)
{
    // hwdec/hwdecMode serialize through the decode-mode helpers so the on-disk
    // value always reflects the effective mode (disabled hwdec ⇒ "off").
    reg.AddBool("playback.hwdec", s.hwdec, "Enable hardware video decoding.",
                [&s]
                {
                    const VideoDecodeMode mode =
                        s.hwdec ? VideoDecodeModeFromString(s.hwdecMode) : VideoDecodeMode::Off;
                    return std::string(IsVideoDecodeModeEnabled(mode) ? "1" : "0");
                });
    reg.AddString("playback.hwdecMode", s.hwdecMode, FRAMELIFT_HWDEC_MODE_DESC,
                  [&s]
                  {
                      const VideoDecodeMode mode =
                          s.hwdec ? VideoDecodeModeFromString(s.hwdecMode) : VideoDecodeMode::Off;
                      return std::string(VideoDecodeModeName(mode));
                  });
    reg.AddBool("playback.hrSeek", s.hrSeek, "Use precise (high-resolution) seeking.");
    reg.AddBool("playback.subAutoLoad", s.subAutoLoad, "Auto-load subtitle files matching the opened media.");
    reg.AddBool("playback.audioFileAutoLoad", s.audioFileAutoLoad,
                "Auto-load external audio files matching the opened media.");

    // Reconcile hwdec/hwdecMode after load: an
    // explicit mode wins; a bare hwdec=0 forces "off"; otherwise default to auto.
    reg.AddPostLoad(
        [&s](const std::set<std::string>& seen)
        {
            if (seen.count("playback.hwdecMode"))
            {
                s.hwdecMode = VideoDecodeModeName(VideoDecodeModeFromString(s.hwdecMode));
                s.hwdec = IsVideoDecodeModeEnabled(VideoDecodeModeFromString(s.hwdecMode));
            }
            else if (seen.count("playback.hwdec") && !s.hwdec)
            {
                s.hwdecMode = VideoDecodeModeName(VideoDecodeMode::Off);
            }
            else
            {
                s.hwdecMode = VideoDecodeModeName(VideoDecodeMode::Auto);
                s.hwdec = true;
            }
        });
}
