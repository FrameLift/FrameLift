#pragma once

#include "AudioSettings.h"
#include "PlaybackSettings.h"
#include "SubtitleSettings.h"
#include "ThemeUtil.h"
#include "VideoDecodeMode.h"
#include <framelift/platform/IMediaPlayer.h>

#include <algorithm>
#include <cmath>
#include <cstring>

// Pure mappers from the media/ffmpeg settings sub-structs to the POD player option
// structs. Header-only so they can be unit-tested without pulling in FFmpeg.

inline AudioNormalizeParams ToAudioNormalizeParams(const AudioSettings& s)
{
    return {s.dynaudnormFrameLen, s.dynaudnormGaussSize, s.dynaudnormPeak, s.dynaudnormMaxGain, s.dynaudnormVolume};
}

inline SeekPrecisionMode ToSeekPrecisionMode(const PlaybackSettings& s)
{
    return SeekPrecisionModeFromString(s.seekMode.c_str());
}

inline PlaybackOptions ToPlaybackOptions(const PlaybackSettings& s)
{
    // The SDK POD's hrSeek bool can't express the three-way seekMode; keep it
    // coherent for observers: false only when every seek snaps to keyframes.
    return {
        IsVideoDecodeModeEnabled(VideoDecodeModeFromString(s.hwdecMode)),
        ToSeekPrecisionMode(s) != SeekPrecisionMode::Keyframe, s.subAutoLoad, s.audioFileAutoLoad
    };
}

inline VideoDecodeMode ToVideoDecodeMode(const PlaybackSettings& s)
{
    return VideoDecodeModeFromString(s.hwdecMode);
}

inline AudioPreferences ToAudioPreferences(const AudioSettings& s)
{
    AudioPreferences prefs;
    std::strncpy(prefs.preferredLang, s.defaultLanguage.c_str(), sizeof(prefs.preferredLang) - 1);
    std::strncpy(prefs.outputDevice, s.outputDevice.c_str(), sizeof(prefs.outputDevice) - 1);
    prefs.defaultVolume = std::clamp(s.defaultVolume, 0, 100);
    prefs.syncOffsetMs = s.syncOffsetMs;
    prefs.channelMode = static_cast<AudioChannelMode>(std::clamp(s.channelMode, 0, 3));
    prefs.duckingEnabled = s.duckingEnabled;
    prefs.duckingLevel = std::clamp(s.duckingLevel, 0, 100);
    prefs.duckingTrigger = AudioDuckingTrigger::Notifications;
    return prefs;
}

inline SubtitleStyle ToSubtitleStyle(const SubtitleSettings& s)
{
    // Pack a "#RRGGBB" colour + an inverted-alpha transparency byte into the ASS
    // 0xRRGGBBAA convention used by libass (AA: 0x00 = opaque, 0xFF = transparent).
    auto pack = [](const std::string& hex, float opacity) -> uint32_t
    {
        float rgb[3] = {0.f, 0.f, 0.f};
        ThemeUtil::ParseHexColor(hex.c_str(), rgb); // leaves rgb at 0 on malformed input
        auto byte = [](float f)
        {
            return static_cast<uint32_t>(std::lround(std::clamp(f, 0.f, 1.f) * 255.f));
        };
        const uint32_t alpha = 255u - byte(opacity); // opacity 1.0 ⇒ alpha 0 (opaque)
        return (byte(rgb[0]) << 24) | (byte(rgb[1]) << 16) | (byte(rgb[2]) << 8) | alpha;
    };

    SubtitleStyle st;
    st.overrideEnabled = s.overrideStyle;
    st.fontScale = s.fontScale;
    std::strncpy(st.fontFamily, s.fontFamily.c_str(), sizeof(st.fontFamily) - 1);
    st.textColor = pack(s.textColor, 1.0f);
    st.outlineColor = pack(s.outlineColor, 1.0f);
    st.backColor = pack(s.backColor, s.backOpacity);
    st.edgeStyle = static_cast<SubtitleEdgeStyle>(std::clamp(s.edgeStyle, 0, 3));
    st.outlineWidth = s.outlineWidth;
    st.shadowDepth = s.shadowDepth;
    st.alignment = (s.alignment >= 1 && s.alignment <= 9) ? s.alignment : 0;
    st.lineSpacing = s.lineSpacing;
    st.letterSpacing = s.letterSpacing;
    std::strncpy(st.preferredLang, s.defaultLanguage.c_str(), sizeof(st.preferredLang) - 1);
    st.preferForced = s.preferForced;
    return st;
}
