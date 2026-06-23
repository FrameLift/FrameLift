#pragma once

#include <framelift/ui/UI.h>

class UIContext;

// ── Window helpers ────────────────────────────────────────────────────────────
// Named flag bundles plus a scoped Begin/End wrapper, so a plugin no longer has to
// hand-assemble a WindowFlags bitmask and pair PushStyle*/Begin with End/PopStyle*
// by hand. Window *positioning* (SetNextWindowPos/Size) stays an explicit caller
// call — only the chrome flags/style and the Begin/End bookkeeping are bundled.

namespace framelift
{
// Common window archetypes as ready-made flag bundles.
namespace WindowPreset
{
// Fixed HUD overlay: no chrome, can't move/resize, stays behind focus, not persisted
// (e.g. the DebugOverlay stats panel).
inline constexpr UI::WindowFlags Hud = UI::WindowFlags::NoTitleBar | UI::WindowFlags::NoResize |
                                       UI::WindowFlags::NoMove | UI::WindowFlags::NoScrollbar |
                                       UI::WindowFlags::NoSavedSettings | UI::WindowFlags::NoBringToFrontOnFocus;

// Floating tool window: titled, user-movable/resizable, not persisted
// (e.g. LogViewer, Benchmark).
inline constexpr UI::WindowFlags Floating = UI::WindowFlags::NoSavedSettings;

// Fullscreen overlay: fills the main viewport, no chrome (e.g. the SettingsMenu).
inline constexpr UI::WindowFlags Fullscreen = UI::WindowFlags::NoTitleBar | UI::WindowFlags::NoResize |
                                              UI::WindowFlags::NoMove | UI::WindowFlags::NoCollapse |
                                              UI::WindowFlags::NoSavedSettings;
} // namespace WindowPreset

// Optional chrome styling applied for the window's lifetime. A negative/false field
// leaves the host default untouched.
struct WindowStyle
{
    float rounding = -1.f;            // WindowRounding; <0 = leave default
    UI::Vec2 padding = {-1.f, -1.f};  // WindowPadding; either component <0 = leave default
    bool hasBg = false;               // when true, push WindowBg = bg
    UI::Color4f bg{};
    float bgAlpha = -1.f;             // SetNextWindowBgAlpha; <0 = leave default
};

// Scoped Begin/End wrapper. The constructor pushes any requested style and calls
// Begin(); the destructor pops exactly what was pushed and calls End(). End() is
// always paired with Begin() regardless of visibility — that matches ImGui's
// contract — so you may early-out on the bool but must still let the object
// destruct normally:
//
//     ScopedWindow w(ctx, "##overlay", WindowPreset::Hud, nullptr,
//                    {.rounding = 4.f, .padding = {10.f, 8.f}});
//     if (w) { ...draw contents... }
//
class ScopedWindow
{
  public:
    ScopedWindow(
        UIContext& ctx, const char* name, UI::WindowFlags flags, bool* open = nullptr, const WindowStyle& style = {}
    ) noexcept;
    ~ScopedWindow() noexcept;

    ScopedWindow(const ScopedWindow&) = delete;
    ScopedWindow& operator=(const ScopedWindow&) = delete;

    [[nodiscard]] bool IsOpen() const noexcept
    {
        return visible_;
    }
    explicit operator bool() const noexcept
    {
        return visible_;
    }

  private:
    UIContext& ctx_;
    int styleVars_ = 0;
    int styleColors_ = 0;
    bool visible_ = false;
};
} // namespace framelift
