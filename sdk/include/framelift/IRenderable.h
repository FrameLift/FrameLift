#pragma once

#include <framelift/ui/UIContext.h>

// Interface for objects that draw UI during a frame.
// Render() is called once per painted frame between IAppWindow::ImGuiBeginFrame()
// and IAppWindow::ImGuiEndFrame(), with an active ImGui frame.
//
// Redraw contract (immediate mode + demand-driven loop):
//   The host does NOT paint every frame — it blocks on events and repaints only when
//   something changed. When it does repaint, it renders the WHOLE UI (every renderable),
//   so a module must always emit its draw calls when visible; never skip drawing based on
//   a data-dirty flag (in immediate mode that erases the widget). Dirty/cache flags gate
//   DATA PROCESSING only (string formatting, expensive queries).
//   To keep the loop awake for an ongoing visual change, call ctx.RequestRedraw() — but
//   ONLY while a real change is in flight: an animation in progress (prefer the Animation
//   primitive / Panel slide), a background/async op that mutates state without posting a
//   wake event, or live playback-driven data. Never unconditionally "while open" — that
//   spins the loop at the redraw cap over nothing.
class IRenderable
{
public:
    static constexpr const char* InterfaceId = "framelift.IRenderable";
    virtual ~IRenderable() = default;

    // Draw UI for this frame. Query the client-area size via ctx.GetMainWindowSize() if
    // needed. See the redraw contract above for when to call ctx.RequestRedraw().
    virtual void Render(UIContext& ctx) noexcept = 0;
};