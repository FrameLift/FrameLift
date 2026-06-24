#include <framelift/ui/Window.h>

#include <framelift/ui/UIContext.h>

namespace framelift
{
ScopedWindow::ScopedWindow(
    UIContext& ctx, const char* name, const UI::WindowFlags flags, bool* open, const WindowStyle& style
) noexcept
    : ctx_(ctx)
{
    if (style.rounding >= 0.f)
    {
        ctx_.PushStyleVar(UI::StyleVar::WindowRounding, style.rounding);
        ++styleVars_;
    }
    if (style.padding.x >= 0.f && style.padding.y >= 0.f)
    {
        ctx_.PushStyleVar(UI::StyleVar::WindowPadding, style.padding);
        ++styleVars_;
    }
    if (style.hasBg)
    {
        ctx_.PushStyleColor(UI::ColorSlot::WindowBg, style.bg);
        ++styleColors_;
    }
    if (style.bgAlpha >= 0.f)
    {
        // "Next window" state, not a stack push — no matching pop in the destructor.
        ctx_.SetNextWindowBgAlpha(style.bgAlpha);
    }

    visible_ = ctx_.Begin(name, open, flags);
}

ScopedWindow::~ScopedWindow() noexcept
{
    // End() always pairs with Begin(), even when Begin() returned false.
    ctx_.End();
    if (styleColors_ > 0)
    {
        ctx_.PopStyleColor(styleColors_);
    }
    if (styleVars_ > 0)
    {
        ctx_.PopStyleVar(styleVars_);
    }
}
} // namespace framelift
