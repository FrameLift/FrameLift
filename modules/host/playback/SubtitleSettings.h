#pragma once

#include "SettingsRegistry.h"

#include <string>

// Subtitle settings — owned by the media/ffmpeg module (libass styling + selection).

struct SubtitleSettings
{
    bool overrideStyle = false;
    float fontScale = 1.0f;
    std::string fontFamily;
    std::string textColor = "#FFFFFF";
    std::string outlineColor = "#000000";
    std::string backColor = "#000000";
    float backOpacity = 0.5f;
    int edgeStyle = 1;
    float outlineWidth = 2.0f;
    float shadowDepth = 0.0f;
    int alignment = 2;
    float lineSpacing = 0.0f;
    float letterSpacing = 0.0f;
    std::string defaultLanguage; // preferred subtitle language (ISO 639 code)
    bool preferForced = false;
};

inline void RegisterSubtitleSettings(SettingsRegistry& reg, SubtitleSettings& s)
{
    reg.AddBool("subtitles.overrideStyle", s.overrideStyle,
                "Override the subtitle file's own styling with the settings below.");
    reg.AddFloat("subtitles.fontScale", s.fontScale, "Subtitle font-size multiplier (1.0 = the file's default size).");
    reg.AddString("subtitles.fontFamily", s.fontFamily, "Subtitle font family; empty keeps the file's font.");
    reg.AddString("subtitles.textColor", s.textColor, "Subtitle text colour as #RRGGBB.");
    reg.AddString("subtitles.outlineColor", s.outlineColor, "Subtitle outline colour as #RRGGBB.");
    reg.AddString("subtitles.backColor", s.backColor, "Subtitle shadow / box background colour as #RRGGBB.");
    reg.AddFloat("subtitles.backOpacity", s.backOpacity, "Opacity of the shadow / box background (0.0-1.0).");
    reg.AddInt("subtitles.edgeStyle", s.edgeStyle, "Edge style: 0 none, 1 outline, 2 drop shadow, 3 opaque box.");
    reg.AddFloat("subtitles.outlineWidth", s.outlineWidth, "Outline thickness in pixels.");
    reg.AddFloat("subtitles.shadowDepth", s.shadowDepth, "Drop-shadow offset in pixels.");
    reg.AddInt("subtitles.alignment", s.alignment, "Numpad alignment 1-9 (\\an); 0 keeps the file's alignment.");
    reg.AddFloat("subtitles.lineSpacing", s.lineSpacing, "Extra space between subtitle lines, pixels.");
    reg.AddFloat("subtitles.letterSpacing", s.letterSpacing, "Extra space between glyphs, pixels.");
    reg.AddString("subtitles.defaultLanguage", s.defaultLanguage,
                  "Preferred subtitle language to auto-select (ISO 639 code, e.g. eng).");
    reg.AddBool("subtitles.preferForced", s.preferForced,
                "Prefer a forced subtitle track when one is available.");
}
