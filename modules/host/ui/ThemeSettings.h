#pragma once

#include "SettingsRegistry.h"

#include <string>

// Theme settings — owned by the host/ui module.

struct ThemeSettings
{
    std::string preset = "dark";
    std::string accentColor = "#4296FA";
    std::string fontFile;
    float fontSize = 16.0f;
};

inline void RegisterThemeSettings(SettingsRegistry& reg, ThemeSettings& s)
{
    reg.AddString("theme.preset", s.preset, "UI color preset: dark|light|classic.");
    reg.AddString("theme.accentColor", s.accentColor, "Accent color as #RRGGBB.");
    reg.AddString("theme.fontFile", s.fontFile, "Absolute path to a TTF font; empty = bundled Roboto.");
    reg.AddFloat("theme.fontSize", s.fontSize, "UI font size in points.");
}
