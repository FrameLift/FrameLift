#pragma once

#include <framelift/core.h>
#include <framelift/platform.h>
#include <framelift/ui.h>

#include <chrono>
#include <memory>
#include <string>

// Combined idle-screen + HUD overlay.
//
//  • When the player is idle – draws the 4-corner gradient welcome screen.
//  • When playing           – on ShowCommand(), fades in a command label
//                             (top-left) and a seek bar (center), then fades out.
//
// Uses GetBackgroundDrawList() for the idle gradient so panels still render
// on top, and GetForegroundDrawList() for the HUD so it sits above panels.
// Route every MediaEvent through HandleMediaEvent().
class Overlay final : public SafeRenderable, public ModuleBase
{
public:
    Overlay() = default;

    // Display `label` and reset the fade timer.
    void ShowCommand(std::string label);

    // True when the player has no file loaded (welcome screen showing).
    [[nodiscard]] bool IsIdle() const noexcept
    {
        return isIdle_;
    }

    bool HandleEvent(const AppEvent& e) override;
    void HandleMediaEvent(const MediaEvent& event) override;

    void OnRender(UIContext& ctx) override;

protected:
    const char* ModuleName() const override
    {
        return "Overlay";
    }

    void OnInstall(IModuleContext& ctx) override;

private:
    // Draw the seek bar and playback controls centred at the bottom of the window.
    void RenderControlsBar(float w, float h, UIContext& ctx);

    IMediaPlayback* playback_ = nullptr;     // transport (pause/seek), from ctx_ in OnInstall()
    IMediaProperties* props_ = nullptr;      // property observation, from ctx_ in OnInstall()
    uintptr_t iconTex_ = 0;          // GPU handle for the icon texture (0 = not loaded)
    bool iconLoadAttempted_ = false; // prevents repeated load attempts after failure

    bool isIdle_ = true; // true when no file is loaded (player is in idle state)
    bool isPaused_ = false;
    double timePos_ = 0.0;  // current playback position in seconds
    double duration_ = 0.0; // total duration of the current file in seconds

    // Cached panel insets / settings visibility, updated via events
    // (PanelLayoutEvent, SettingsVisibilityEvent).
    float leftInset_ = 0.f;
    float rightInset_ = 0.f;
    bool settingsOpen_ = false;

    std::string commandLabel_;
    // Initialised far in the past so the HUD is invisible until ShowCommand() is called.
    std::chrono::steady_clock::time_point shownAt_{std::chrono::steady_clock::now() - std::chrono::seconds(9999)};

    // Controls bar: shown on mouse activity, auto-hides after inactivity.
    std::chrono::steady_clock::time_point mouseActiveAt_{std::chrono::steady_clock::now() - std::chrono::seconds(9999)};
    bool isDraggingSeek_ = false; // true while the user drags the seek bar thumb

    static constexpr double fadeDelay = 2.0; // seconds the command label is held before fading
    static constexpr double fadeDur = 0.4;   // fade-out duration in seconds
    static constexpr double barVisible = 1.5;
    // seconds the controls bar stays visible after mouse activity
    static constexpr double barFade = 0.25; // controls bar fade-out duration in seconds
};

FRAMELIFT_MODULE_ENTRY(Overlay, {
    .renderOrder = 0,
})
