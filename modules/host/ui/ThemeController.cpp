#include "ThemeController.h"

#include "Settings.h"
#include "Theme.h"

#include <framelift/ContextHelpers.h>

ThemeController::Snapshot ThemeController::Take(const ThemeSettings& s)
{
    return {s.preset, s.accentColor, s.fontFile, s.fontSize};
}

void ThemeController::ApplyInitial(const ThemeSettings& s)
{
    Theme::ApplyStyle(s);
    Theme::RebuildFonts(s);
    applied_ = Take(s);
}

void ThemeController::Connect(IModuleContext& ctx, const Settings& settings)
{
    framelift::RegisterSettingsChangeCallback(ctx, [this, &settings] { OnSettingsChanged(settings.Get<ThemeSettings>()); });
}

void ThemeController::OnSettingsChanged(const ThemeSettings& s)
{
    const Snapshot next = Take(s);
    if (next.preset != applied_.preset || next.accentColor != applied_.accentColor)
    {
        styleDirty_ = true;
    }
    if (next.fontFile != applied_.fontFile || next.fontSize != applied_.fontSize)
    {
        fontAtlasDirty_ = true;
    }
    applied_ = next;
}

void ThemeController::ApplyPending(const ThemeSettings& s)
{
    if (styleDirty_)
    {
        styleDirty_ = false;
        Theme::ApplyStyle(s);
    }
    if (fontAtlasDirty_)
    {
        fontAtlasDirty_ = false;
        Theme::RebuildFonts(s);
    }
}
