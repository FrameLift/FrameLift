#include <algorithm>
#include <cmath>
#include <framelift/Events.h>
#include <framelift/FocusManager.h>
#include <framelift/Guard.h>
#include <framelift/IModuleContext.h>
#include <framelift/platform/IAppWindow.h>
#include <framelift/services/ISettingsStore.h>
#include <framelift/ui/Panel.h>
#include <framelift/ui/UIContext.h>

Panel::Panel(const Side side, const float defaultWidth, const char* title)
    : side_(side), defaultWidth_(defaultWidth), title_(title ? title : "Panel"),
      animX_(side == Side::Left ? -defaultWidth : defaultWidth)
{
}

void Panel::SetFocusManager(FocusManager* fm, IModule* self)
{
    focusManager_ = fm;
    selfModule_ = self;
}

void Panel::Toggle()
{
    SetOpen(!open_);
}

void Panel::SetOpen(const bool v)
{
    const bool wasOpen = open_;
    open_ = v;
    if (!wasOpen && open_)
    {
        if (focusManager_)
        {
            focusManager_->Acquire(selfModule_);
        }
        OnOpened();
    }
    else if (wasOpen && !open_)
    {
        // Closing a popped-out panel re-docks it to the edge (hidden, snapped - no
        // slide) so the next open uses the normal docked slide-in.
        if (poppedOut_)
        {
            poppedOut_ = false;
            animX_ = side_ == Side::Left ? -GetWidth() : GetWidth();
        }
        if (focusManager_)
        {
            focusManager_->Release(selfModule_);
        }
    }
}

float Panel::GetWidth() const
{
    auto* store = panelCtx_ ? panelCtx_->GetService<ISettingsStore>() : nullptr;
    return store ? store->GetSettingFloat("ui.panelWidth") : defaultWidth_;
}

float Panel::GetSlideSpeed() const
{
    auto* store = panelCtx_ ? panelCtx_->GetService<ISettingsStore>() : nullptr;
    return store ? store->GetSettingFloat("ui.slideSpeed") : 18.f;
}

float Panel::PanelWidth() const
{
    return GetWidth();
}

float Panel::VisibleWidth() const
{
    if (poppedOut_)
    {
        return 0.f; // the edge is clear while the panel lives in its own window
    }
    const float w = GetWidth();
    if (side_ == Side::Left)
    {
        return w + animX_; // animX_ is 0 when open, -w when hidden
    }
    return w - animX_; // animX_ is 0 when open, +w when hidden
}

bool Panel::IsActive() const noexcept
{
    if (poppedOut_)
    {
        return true; // popped-out OS window still needs its content submitted
    }
    const float w = GetWidth();
    if (open_)
    {
        return true;
    }
    if (side_ == Side::Left)
    {
        return animX_ > -w + 1.f;
    }
    return animX_ < w - 1.f;
}

void Panel::Render(UIContext& ctx) noexcept
{
    const UI::Vec2 mainSize = ctx.GetMainWindowSize();
    const int windowW = static_cast<int>(mainSize.x);
    const int windowH = static_cast<int>(mainSize.y);

    if (poppedOut_)
    {
        // Report 0 width so layout consumers (e.g. Overlay's controls bar)
        // reclaim the full main window while the panel is detached.
        if (panelCtx_ && lastPublishedWidth_ != 0.f)
        {
            lastPublishedWidth_ = 0.f;
            panelCtx_->Publish<PanelLayoutEvent>({side_ == Side::Left ? 0 : 1, 0.f});
        }
        RenderPoppedOut(windowW, windowH, ctx);
        return;
    }

    const float w = GetWidth();

    // -- Slide animation -------------------------------------------------------
    const float targetX = open_ ? 0.f : side_ == Side::Left ? -w : w;

    const float speed = GetSlideSpeed() * ctx.GetDeltaTime();
    if (std::abs(animX_ - targetX) < 0.5f)
    {
        animX_ = targetX;
    }
    else
    {
        animX_ += (targetX - animX_) * std::min(speed, 1.f);
        // Still sliding: ask the host to paint the next frame so the animation runs to
        // completion under the demand-driven render loop, then settles and lets it sleep.
        ctx.RequestRedraw();
    }

    // Broadcast the animated width so other modules (e.g. Overlay's controls
    // bar) can inset around the panel. Published before the early-return so
    // the final settle-to-hidden frame still reports width 0.
    if (panelCtx_)
    {
        const float visible = VisibleWidth();
        if (visible != lastPublishedWidth_)
        {
            lastPublishedWidth_ = visible;
            panelCtx_->Publish<PanelLayoutEvent>({side_ == Side::Left ? 0 : 1, visible});
        }
    }

    // A fully-closed, settled panel needs no window submitted this frame.
    if (!IsActive())
    {
        return;
    }

    // -- Panel position --------------------------------------------------------
    const float posX = side_ == Side::Left ? animX_ : static_cast<float>(windowW) - w + animX_;

    // Keep the docked panel inside the host window: while it slides off-screen on
    // close its position passes the main window's edge, and without this the
    // multi-viewport backend would detach it into its own OS window mid-animation.
    ctx.PinNextWindowToMainViewport();
    ctx.SetNextWindowPos(UI::Vec2(posX, 0.f), UI::Cond::Always);
    ctx.SetNextWindowSize(UI::Vec2(w, static_cast<float>(windowH)), UI::Cond::Always);
    ctx.SetNextWindowBgAlpha(0.92f);

    const UI::WindowFlags flags = UI::WindowFlags::NoTitleBar | UI::WindowFlags::NoResize | UI::WindowFlags::NoMove |
                                  UI::WindowFlags::NoScrollbar | UI::WindowFlags::NoSavedSettings |
                                  UI::WindowFlags::NoBringToFrontOnFocus;

    ctx.PushStyleVar(UI::StyleVar::WindowRounding, 0.f);
    ctx.PushStyleVar(UI::StyleVar::WindowPadding, UI::Vec2(0.f, 0.f));
    ctx.PushStyleColor(UI::ColorSlot::WindowBg, UI::Color4f(0.08f, 0.05f, 0.12f, 0.95f));
    ctx.PushStyleColor(UI::ColorSlot::Border, UI::Color4f(0.3f, 0.2f, 0.5f, 0.5f));

    if (ctx.Begin(side_ == Side::Left ? "##panel_left" : "##panel_right", nullptr, flags))
    {
        // Guarded here (not around the whole frame) so the style pushes and
        // Begin/End above stay balanced when content throws.
        framelift::Guard(
            "Panel content",
            [&]
            {
                RenderContent(w, static_cast<float>(windowH), ctx);
            }
        );
        // Drawn after the content so it sits on top of the plugin's header bar.
        DrawPopToggle(ctx);
    }
    ctx.End();

    ctx.PopStyleColor(2);
    ctx.PopStyleVar(2);
}

void Panel::RenderPoppedOut(const int windowW, const int windowH, UIContext& ctx)
{
    const float w = GetWidth();

    if (justPoppedOut_)
    {
        // Seed the window beside the main window on the panel's anchored side: a
        // left panel pops out to the left, a right panel to the right. The host
        // maps these (main-window-relative) coordinates to screen space outside the
        // host viewport, so ImGui spawns a real OS window; afterwards the user owns
        // its position and size.
        constexpr float gap = 30.f;
        const float popH = static_cast<float>(windowH) * 0.7f;

        // Work in screen space so we can clamp against the monitor's usable bounds,
        // then convert back to the main-window-relative coordinates SetNextWindowPos
        // expects.
        const UI::Vec2 mainPos = ctx.GetMainWindowScreenPos();
        float screenX = side_ == Side::Left ? mainPos.x - w - gap : mainPos.x + static_cast<float>(windowW) + gap;
        float screenY = mainPos.y + 40.f;

        if (auto* win = panelCtx_ ? panelCtx_->GetService<IAppWindow>() : nullptr)
        {
            // Keep the whole window on the display the main window lives on, so a
            // maximized or edge-docked app never spawns the pop-out off-screen.
            const Rect b = win->GetDisplayUsableBounds();
            if (b.w > 0 && b.h > 0)
            {
                screenX = std::clamp(screenX, static_cast<float>(b.x), static_cast<float>(b.x + b.w) - w);
                screenY = std::clamp(screenY, static_cast<float>(b.y), static_cast<float>(b.y + b.h) - popH);
            }
        }

        ctx.SetNextWindowPos(UI::Vec2(screenX - mainPos.x, screenY - mainPos.y), UI::Cond::Always);
        ctx.SetNextWindowSize(UI::Vec2(w, popH), UI::Cond::Always);
        justPoppedOut_ = false;
    }

    // A normal titled/movable/resizable window. NoSavedSettings: pop-out state is
    // intentionally per-session and must not leak into imgui.ini.
    const UI::WindowFlags flags =
        UI::WindowFlags::NoScrollbar | UI::WindowFlags::NoCollapse | UI::WindowFlags::NoSavedSettings;

    ctx.PushStyleVar(UI::StyleVar::WindowRounding, 0.f);
    ctx.PushStyleVar(UI::StyleVar::WindowPadding, UI::Vec2(0.f, 0.f));
    ctx.PushStyleColor(UI::ColorSlot::WindowBg, UI::Color4f(0.08f, 0.05f, 0.12f, 1.f)); // opaque for an OS window
    ctx.PushStyleColor(UI::ColorSlot::Border, UI::Color4f(0.3f, 0.2f, 0.5f, 0.5f));

    bool keepOpen = true;
    if (ctx.Begin(title_, &keepOpen, flags))
    {
        // Render inside a child so the content's coordinate origin sits just below
        // the title bar. The docked panel has no title bar, so RenderContent uses
        // absolute SetCursorPosY() values that assume the origin is the very top;
        // without this child those rows would draw behind the title bar.
        ctx.BeginChild("##popbody", UI::Vec2(0.f, 0.f));
        const float cw = ctx.GetWindowWidth();
        const float ch = ctx.GetWindowHeight();
        framelift::Guard(
            "Panel content",
            [&]
            {
                RenderContent(cw, ch, ctx);
            }
        );
        DrawPopToggle(ctx);
        ctx.EndChild();
    }
    ctx.End();

    ctx.PopStyleColor(2);
    ctx.PopStyleVar(2);

    if (!keepOpen)
    {
        // The title-bar close button docks the panel back to the edge (open),
        // rather than hiding it outright.
        poppedOut_ = false;
        open_ = true;
        animX_ = 0.f;
    }
}

bool Panel::DrawPopToggle(UIContext& ctx)
{
    constexpr float pad = 12.f, bw = 22.f, bh = 20.f;
    const float w = ctx.GetWindowWidth();

    ctx.SetCursorPosY(8.f);
    ctx.SetCursorPosX(w - pad - bw);
    const UI::Vec2 m = ctx.GetCursorScreenPos();

    ctx.PushStyleColor(UI::ColorSlot::Button, UI::Color4f(0.15f, 0.10f, 0.25f, 0.70f));
    ctx.PushStyleColor(UI::ColorSlot::ButtonHovered, UI::Color4f(0.25f, 0.20f, 0.40f, 0.85f));
    const bool clicked = ctx.Button("##panelPop", UI::Vec2(bw, bh));
    const bool hovered = ctx.IsItemHovered();
    ctx.PopStyleColor(2);

    // "Open in new window" glyph: a small square with an arrow leaving its corner.
    auto& dl = ctx.GetWindowDrawList();
    const UI::Color32 ink = UI::MakeColor32(205, 195, 235, 255);
    const float x = m.x + 4.f, y = m.y + 4.f;
    const UI::Vec2 a{x, y + 4.f}, b{x + 9.f, y + 4.f}, c{x + 9.f, y + 12.f}, d{x, y + 12.f};
    dl.AddLine(a, b, ink);
    dl.AddLine(b, c, ink);
    dl.AddLine(c, d, ink);
    dl.AddLine(d, a, ink);
    const UI::Vec2 tail{x + 7.f, y + 6.f}, head{x + 14.f, y};
    dl.AddLine(tail, head, ink);
    dl.AddLine(head, UI::Vec2(head.x - 5.f, head.y), ink);
    dl.AddLine(head, UI::Vec2(head.x, head.y + 5.f), ink);

    if (hovered && ctx.BeginTooltip())
    {
        ctx.Text(poppedOut_ ? "Dock to edge" : "Pop out to window");
        ctx.EndTooltip();
    }

    if (clicked)
    {
        if (poppedOut_)
        {
            poppedOut_ = false;
            open_ = true;
            animX_ = 0.f;
        }
        else
        {
            poppedOut_ = true;
            justPoppedOut_ = true;
        }
    }
    return clicked;
}