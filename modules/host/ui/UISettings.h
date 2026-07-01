#pragma once

#include "SettingsRegistry.h"

// Side-panel UI settings — owned by the host/ui module.

struct UISettings
{
    float panelWidth = 320.f;
    float slideSpeed = 18.f;
};

inline void RegisterUISettings(SettingsRegistry& reg, UISettings& s)
{
    reg.AddFloat("ui.panelWidth", s.panelWidth, "Width in pixels of the side panels (playlist, etc.).");
    reg.AddFloat("ui.slideSpeed", s.slideSpeed, "Side drawer slide-in/out animation speed.");
}
