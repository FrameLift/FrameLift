#pragma once

#include <cstdint>
#include <framelift/IRenderable.h>

class FocusManager;
class IModule;
class IModuleContext;

// Reusable slide-in panel that can be anchored to the left or right edge.
// Subclasses implement RenderContent() to fill the panel body.
class Panel : public IRenderable
{
public:
    enum class Side : std::uint8_t
    {
        Left,
        Right
    };

    // `title` is the caption shown when the panel is popped out into its own OS
    // window (string-literal lifetime - stored by pointer, not copied).
    explicit Panel(Side side = Side::Left, float defaultWidth = 320.f, const char* title = "Panel");

    // Provide the plugin context so the panel reads ui.panelWidth/ui.slideSpeed
    // live via the ABI-stable per-key setting getters.
    void SetContext(IModuleContext* ctx)
    {
        panelCtx_ = ctx;
    }

    // Register a focus manager so the panel acquires/releases keyboard focus
    // automatically on open and close.
    void SetFocusManager(FocusManager* fm, IModule* self);

    // -- Visibility -------------------------------------------------------------
    [[nodiscard]] bool IsOpen() const
    {
        return open_;
    }

    // Flip the open/closed state; calls OnOpened() when transitioning closed to open.
    void Toggle();

    // Set open state explicitly; calls OnOpened() when transitioning closed to open.
    void SetOpen(bool v);

    // Current animated visible width in pixels (0 = fully hidden, PanelWidth() = fully open).
    // Reports 0 while popped out, since the edge no longer occupies the main window.
    [[nodiscard]] float VisibleWidth() const;

    // -- Pop-out ----------------------------------------------------------------
    // True while the panel is detached into its own OS window.
    [[nodiscard]] bool IsPoppedOut() const
    {
        return poppedOut_;
    }

    // Animate and draw the panel frame. Sealed so subclasses override RenderContent().
    void Render(UIContext& ctx) noexcept final;

protected:
    // Called once each time the panel transitions from closed to open.
    // Use to reset cursor state, scroll position, etc.
    virtual void OnOpened()
    {
    }

    [[nodiscard]] IModuleContext* GetContext() const
    {
        return panelCtx_;
    }

    // Draw the panel's contents inside the animated panel area.
    // panelW/panelH are the usable interior dimensions in pixels.
    virtual void RenderContent(float panelW, float panelH, UIContext& ctx) = 0;

    // Current effective panel width: from settings if available, otherwise defaultWidth_.
    [[nodiscard]] float PanelWidth() const;

private:
    // Effective width - reads from settings when available.
    [[nodiscard]] float GetWidth() const;
    // Effective slide speed - reads from settings when available.
    [[nodiscard]] float GetSlideSpeed() const;

    // Draw the panel content in its own OS window (multi-viewport).
    void RenderPoppedOut(int windowW, int windowH, UIContext& ctx);
    // True while the panel is animating, open, or popped out - i.e. its window
    // should still be submitted this frame.
    [[nodiscard]] bool IsActive() const noexcept;
    // Draw the small pop-out/dock toggle in the panel's top-right corner.
    // Returns true on the click that flips poppedOut_ this frame.
    bool DrawPopToggle(UIContext& ctx);

    Side side_;
    float defaultWidth_;
    const char* title_;
    float animX_; // current animated x-offset (0 = fully open, +/-width = fully hidden)
    bool open_ = false;
    bool poppedOut_ = false;     // detached into its own OS window
    bool justPoppedOut_ = false; // set the frame pop-out begins, to seed window pos/size once
    float lastPublishedWidth_ = -1.f; // last VisibleWidth() sent via PanelLayoutEvent
    IModuleContext* panelCtx_ = nullptr;
    FocusManager* focusManager_ = nullptr;
    IModule* selfModule_ = nullptr;
};
