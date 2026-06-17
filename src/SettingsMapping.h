#pragma once

#include "Settings.h"
#include "ThemeUtil.h"
#include <framelift/platform/IMediaPlayer.h>

#include <algorithm>
#include <cmath>
#include <cstring>

// Pure mappers from the host Settings struct to the POD player option structs.
// Extracted from App.cpp so they can be unit-tested without pulling in SDL/FFmpeg.

inline AudioNormalizeParams ParamsFromSettings(const Settings& s)
{
    return {s.dynaudnormFrameLen, s.dynaudnormGaussSize, s.dynaudnormPeak, s.dynaudnormMaxGain, s.dynaudnormVolume};
}

inline PlaybackOptions PlaybackOptsFromSettings(const Settings& s)
{
    return {s.hwdec, s.hrSeek, s.subAutoLoad, s.audioFileAutoLoad};
}

inline ReadAheadCacheOptions ReadAheadOptsFromSettings(const Settings& s)
{
    const int64_t mb = s.readAheadSizeMB > 0 ? s.readAheadSizeMB : 0;
    return {s.readAheadEnabled, mb * 1024 * 1024};
}

inline AudioPreferences AudioPrefsFromSettings(const Settings& s)
{
    AudioPreferences prefs;
    std::strncpy(prefs.preferredLang, s.defaultAudioLanguage.c_str(), sizeof(prefs.preferredLang) - 1);
    std::strncpy(prefs.outputDevice, s.outputDevice.c_str(), sizeof(prefs.outputDevice) - 1);
    prefs.defaultVolume = std::clamp(s.defaultVolume, 0, 100);
    prefs.syncOffsetMs = s.syncOffsetMs;
    prefs.channelMode = static_cast<AudioChannelMode>(std::clamp(s.channelMode, 0, 3));
    prefs.duckingEnabled = s.duckingEnabled;
    prefs.duckingLevel = std::clamp(s.duckingLevel, 0, 100);
    prefs.duckingTrigger = AudioDuckingTrigger::Notifications;
    return prefs;
}

inline SubtitleStyle SubtitleStyleFromSettings(const Settings& s)
{
    // Pack a "#RRGGBB" colour + an inverted-alpha transparency byte into the ASS
    // 0xRRGGBBAA convention used by libass (AA: 0x00 = opaque, 0xFF = transparent).
    auto pack = [](const std::string& hex, float opacity) -> uint32_t
    {
        float rgb[3] = {0.f, 0.f, 0.f};
        ThemeUtil::ParseHexColor(hex.c_str(), rgb); // leaves rgb at 0 on malformed input
        auto byte = [](float f) { return static_cast<uint32_t>(std::lround(std::clamp(f, 0.f, 1.f) * 255.f)); };
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
