#pragma once

#include "SettingsRegistry.h"

// Side-panel UI settings — owned by the host/ui module.

struct UiSettings
{
    float panelWidth = 320.f;
    float slideSpeed = 18.f;
};

inline void RegisterUiSettings(SettingsRegistry& reg, UiSettings& s)
{
    reg.AddFloat("ui.panelWidth", s.panelWidth, "Width in pixels of the side panels (playlist, etc.).");
    reg.AddFloat("ui.slideSpeed", s.slideSpeed, "Panel slide-in/out animation speed.");
}
