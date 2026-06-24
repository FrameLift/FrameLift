#pragma once

#include "SettingsRegistry.h"

#include <string>

// Audio settings — owned by the host/audio module.

struct AudioSettings
{
    std::string defaultLanguage; // preferred audio language (ISO 639 code)
    std::string outputDevice;
    int defaultVolume = 100;
    int syncOffsetMs = 0;
    int channelMode = 0;
    bool duckingEnabled = false;
    int duckingLevel = 50;
    bool normalizeEnabled = false;
    int dynaudnormFrameLen = 100;
    int dynaudnormGaussSize = 5;
    float dynaudnormPeak = 0.95f;
    float dynaudnormMaxGain = 5.0f;
    float dynaudnormVolume = 1.5f;
};

inline void RegisterAudioSettings(SettingsRegistry& reg, AudioSettings& s)
{
    reg.AddString("audio.defaultLanguage", s.defaultLanguage,
                  "Preferred audio language to auto-select (ISO 639 code, e.g. eng).");
    reg.AddString("audio.outputDevice", s.outputDevice,
                  "Preferred audio output device name; empty uses the system default.");
    reg.AddInt("audio.defaultVolume", s.defaultVolume, "Default playback volume (0-100).");
    reg.AddInt("audio.syncOffsetMs", s.syncOffsetMs,
               "Audio sync offset in milliseconds; positive delays audio relative to video.");
    reg.AddInt("audio.channelMode", s.channelMode, "Audio channel mode: 0 auto, 1 mono, 2 stereo, 3 surround.");
    reg.AddBool("audio.duckingEnabled", s.duckingEnabled,
                "Reduce playback volume while app-owned transient audio is active.");
    reg.AddInt("audio.duckingLevel", s.duckingLevel, "Playback gain while ducked, as percent of current volume.");
    reg.AddBool("audio.normalizeEnabled", s.normalizeEnabled, "Enable dynamic audio normalization by default.");
    reg.AddInt("audio.dynaudnormFrameLen", s.dynaudnormFrameLen,
               "Dynamic audio normalization: filter frame length in milliseconds.");
    reg.AddInt("audio.dynaudnormGaussSize", s.dynaudnormGaussSize,
               "Dynamic audio normalization: Gaussian filter window size (odd number).");
    reg.AddFloat("audio.dynaudnormPeak", s.dynaudnormPeak,
                 "Dynamic audio normalization: target peak magnitude (0.0-1.0).");
    reg.AddFloat("audio.dynaudnormMaxGain", s.dynaudnormMaxGain,
                 "Dynamic audio normalization: maximum gain factor.");
    reg.AddFloat("audio.dynaudnormVolume", s.dynaudnormVolume,
                 "Dynamic audio normalization: target RMS volume factor.");
}
