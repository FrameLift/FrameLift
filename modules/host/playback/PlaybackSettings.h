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
    std::string hwdecMode = "auto";
    bool hrSeek = true;
    bool subAutoLoad = true;
    bool audioFileAutoLoad = true;
    bool fastProbe = false;
};

inline void RegisterPlaybackSettings(SettingsRegistry& reg, PlaybackSettings& s)
{
    reg.AddString("playback.hwdecMode", s.hwdecMode, FRAMELIFT_HWDEC_MODE_DESC,
                  [&s]
                  {
                      const VideoDecodeMode mode = VideoDecodeModeFromString(s.hwdecMode);
                      return std::string(VideoDecodeModeName(mode));
                  });
    reg.AddBool("playback.hrSeek", s.hrSeek, "Use precise (high-resolution) seeking.");
    reg.AddBool("playback.fastProbe", s.fastProbe,
                "Speed up file opening by limiting stream probing. Clean MP4/MKV files open faster; "
                "unusual containers (TS/AVI) may misdetect tracks — leave off if tracks go missing.");
    reg.AddBool("playback.subAutoLoad", s.subAutoLoad, "Auto-load subtitle files matching the opened media.");
    reg.AddBool("playback.audioFileAutoLoad", s.audioFileAutoLoad,
                "Auto-load external audio files matching the opened media.");

    // Normalize the mode after load so aliases or invalid values settle before
    // they reach the player.
    reg.AddPostLoad(
        [&s](const std::set<std::string>& seen)
        {
            if (seen.count("playback.hwdecMode"))
            {
                s.hwdecMode = VideoDecodeModeName(VideoDecodeModeFromString(s.hwdecMode));
            }
        });
}
