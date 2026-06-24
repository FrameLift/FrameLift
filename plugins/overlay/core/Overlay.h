#pragma once

#include <framelift/core.h>
#include <framelift/platform.h>
#include <framelift/ui.h>

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
    // HUD command label: held 2.0s after ShowCommand(), then fades out over 0.4s.
    framelift::Animation hud_{0.4f /*fade*/, 2.0f /*hold*/};
    // Controls bar: shown on mouse activity, held 1.5s, then fades out over 0.25s. While
    // either animation is running its Value(ctx) requests the next frame, so the loop
    // animates exactly as long as the fade lasts and then sleeps (no host-side keep-alive).
    framelift::Animation bar_{0.25f /*fade*/, 1.5f /*hold*/};
    bool isDraggingSeek_ = false; // true while the user drags the seek bar thumb
};

FRAMELIFT_MODULE_ENTRY(Overlay, {
    .renderOrder = 0,
})
