#pragma once

#include "ThemeSettings.h"

#include <string>

class IModuleContext;
class Settings;

// Owns the deferred theme-apply state machine. Theme::ApplyStyle / RebuildFonts
// touch global ImGui state and MUST run outside any NewFrame()..Render() pair, but
// settings changes arrive mid-frame (during SettingsMenu's Save inside Render). So
// OnSettingsChanged only flags what changed, and ApplyPending does the work at the
// top of the next frame.
class ThemeController
{
public:
    // Apply immediately and seed the snapshot. Call once before the first frame.
    void ApplyInitial(const ThemeSettings& s);

    // Subscribe to settings changes so theme edits re-apply (deferred to next frame
    // via ApplyPending). settings must outlive this controller.
    void Connect(IModuleContext& ctx, const Settings& settings);

    // Diff against the last-applied snapshot and flag style/font rebuilds. Safe to
    // call mid-frame.
    void OnSettingsChanged(const ThemeSettings& s);

    // Apply any pending style/font rebuild. Call at the top of the frame, before
    // UIBeginFrame.
    void ApplyPending(const ThemeSettings& s);

private:
    struct Snapshot
    {
        std::string preset;
        std::string accentColor;
        std::string fontFile;
        float fontSize = 0.f;
    };
    static Snapshot Take(const ThemeSettings& s);

    Snapshot applied_;
    bool styleDirty_ = false;
    bool fontAtlasDirty_ = false;
};
