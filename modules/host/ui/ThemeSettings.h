#pragma once

#include "SettingsRegistry.h"

#include <string>

// FLTheme settings — owned by the host/ui module.

struct ThemeSettings
{
    std::string preset = "dark";
    std::string accentColor = "#4296FA";
};

inline void RegisterThemeSettings(SettingsRegistry& reg, ThemeSettings& s)
{
    reg.AddString("theme.preset", s.preset, "UI color preset: dark|light|classic.");
    reg.AddString("theme.accentColor", s.accentColor, "Accent color as #RRGGBB.");
}
