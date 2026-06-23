#include <framelift/ui/UI.h>
#include <framelift/ui/UIContext.h>
#include <framelift/ui/Widgets.h>
#include <string>

// ── Internal helpers ──────────────────────────────────────────────────────────

namespace
{
constexpr float RowSpacing = 4.f; // extra vertical gap added after each row
constexpr float BindW = 130.f; // width of the binding text column
constexpr float BtnW = 60.f; // width of Rebind / Clear buttons
constexpr float BtnGap = 4.f; // horizontal gap between buttons

// Render the label on its own line and return the hidden id ("##label") for the
// control that follows, so the label is shown above rather than inline-right.
std::string LabelAbove(UIContext& ctx, const char* label)
{
    ctx.Text(label);
    return std::string("##") + label;
}

// Always-visible muted help text rendered beneath a control. No-op when empty.
void Description(UIContext& ctx, const char* description)
{
    if (description && description[0])
    {
        ctx.TextDisabledWrapped(description);
    }
}
} // namespace

// ── Widget implementations ────────────────────────────────────────────────────

bool Widgets::SliderFloat(
    UIContext& ctx, const char* label, const char* description, float& value, const float min, const float max
)
{
    const std::string id = LabelAbove(ctx, label);
    ctx.SetNextItemWidth(-1.f);
    const bool changed = ctx.SliderFloat(id.c_str(), &value, min, max);
    Description(ctx, description);
    ctx.Dummy({0.f, RowSpacing});
    return changed;
}

bool Widgets::SliderInt(
    UIContext& ctx, const char* label, const char* description, int& value, const int min, const int max
)
{
    const std::string id = LabelAbove(ctx, label);
    ctx.SetNextItemWidth(-1.f);
    const bool changed = ctx.SliderInt(id.c_str(), &value, min, max);
    Description(ctx, description);
    ctx.Dummy({0.f, RowSpacing});
    return changed;
}

bool Widgets::Checkbox(UIContext& ctx, const char* label, const char* description, bool& value)
{
    // Checkbox keeps the inline "[x] label" form — a tiny box under a label reads
    // wrong — but gains the muted description line for parity with the others.
    const bool changed = ctx.Checkbox(label, &value);
    Description(ctx, description);
    ctx.Dummy({0.f, RowSpacing});
    return changed;
}

bool Widgets::InputText(
    UIContext& ctx, const char* label, const char* description, std::string& value, const int maxLen
)
{
    const std::string id = LabelAbove(ctx, label);
    ctx.SetNextItemWidth(-1.f);
    const bool changed = ctx.InputText(id.c_str(), value, maxLen);
    Description(ctx, description);
    ctx.Dummy({0.f, RowSpacing});
    return changed;
}

void Widgets::SectionHeader(UIContext& ctx, const char* label)
{
    ctx.TextColored(UI::Color4f(0.75f, 0.60f, 1.0f, 1.f), label);
    ctx.SeparatorLine(UI::MakeColor32(110, 80, 160, 180));
    ctx.Dummy({0.f, RowSpacing});
}

bool Widgets::Combo(
    UIContext& ctx, const char* label, const char* description, const char* const* items, const int count, int& index
)
{
    if (count <= 0)
    {
        return false;
    }
    if (index < 0 || index >= count)
    {
        index = 0;
    }

    const std::string id = LabelAbove(ctx, label);
    ctx.SetNextItemWidth(-1.f);

    bool changed = false;
    if (ctx.BeginCombo(id.c_str(), items[index]))
    {
        for (int i = 0; i < count; ++i)
        {
            if (ctx.Selectable(items[i], i == index, UI::SelectableFlags::None, {}))
            {
                index = i;
                changed = true;
            }
        }
        ctx.EndCombo();
    }

    Description(ctx, description);
    ctx.Dummy({0.f, RowSpacing});
    return changed;
}

bool Widgets::ColorEdit(UIContext& ctx, const char* label, const char* description, float rgb[3])
{
    const std::string id = LabelAbove(ctx, label);
    ctx.SetNextItemWidth(-1.f);
    const bool changed = ctx.ColorEdit3(id.c_str(), rgb);
    Description(ctx, description);
    ctx.Dummy({0.f, RowSpacing});
    return changed;
}

Widgets::KeybindAction Widgets::KeybindRow(
    UIContext& ctx, const char* label, const std::string& binding, const bool isCapturing
)
{
    auto result = KeybindAction::None;

    ctx.Text(label);
    ctx.SameLine();

    // Right-align: [binding text]  [Rebind]  [Clear]
    // When capturing:              [Press key...]  [Cancel]
    const float rightEdge = ctx.GetWindowWidth() - 8.f;
    const float firstBtnX = isCapturing ? rightEdge - BtnW : rightEdge - 2.f * BtnW - BtnGap;
    const float bindTextX = firstBtnX - BtnGap - BindW;

    ctx.SetCursorPosX(bindTextX);
    if (isCapturing)
    {
        ctx.TextColored(UI::Color4f(1.f, 0.80f, 0.20f, 1.f), "Press key...");
    }
    else
    {
        ctx.TextColored(UI::Color4f(0.75f, 0.65f, 1.f, 1.f), binding.empty() ? "(none)" : binding.c_str());
    }

    ctx.SameLine();
    ctx.SetCursorPosX(firstBtnX);
    if (ctx.Button(isCapturing ? "Cancel" : "Rebind", {BtnW, 0.f}))
    {
        result = isCapturing ? KeybindAction::CancelCapture : KeybindAction::StartCapture;
    }

    if (!isCapturing)
    {
        ctx.SameLine();
        ctx.SetCursorPosX(rightEdge - BtnW);
        if (ctx.Button("Clear", {BtnW, 0.f}))
        {
            result = KeybindAction::Clear;
        }
    }

    ctx.Dummy({0.f, RowSpacing});
    return result;
}

// ── Panel header ──────────────────────────────────────────────────────────────

namespace
{
constexpr float HeaderPadding = 12.f; // left/right inset for header content
constexpr float PopReserve = 26.f;    // right-edge space reserved for the Panel pop-out toggle
constexpr float HeaderBtnGap = 4.f;   // horizontal gap between action buttons
} // namespace

void Widgets::PanelHeader(
    UIContext& ctx, const float panelW, const float headerH, const char* title, const bool poppedOut,
    const char* counter, const float counterInset
)
{
    const UI::Vec2 hdrMin = ctx.GetCursorScreenPos();
    const UI::Vec2 hdrMax = {hdrMin.x + panelW, hdrMin.y + headerH};
    DrawList& dl = ctx.GetWindowDrawList();

    dl.AddRectFilled(hdrMin, hdrMax, UI::MakeColor32(18, 10, 28, 230));

    // Title – top-left (suppressed when popped out: the OS title bar shows it).
    float counterX = HeaderPadding;
    if (!poppedOut && title && title[0])
    {
        ctx.SetCursorPosY(10.f);
        ctx.SetCursorPosX(HeaderPadding);
        ctx.TextColored(UI::Color4f(0.88f, 0.82f, 1.f, 1.f), title);
        counterX = HeaderPadding + counterInset;
    }

    // Counter – beside the title.
    if (counter && counter[0])
    {
        ctx.SetCursorPosY(10.f);
        ctx.SetCursorPosX(counterX);
        ctx.TextColored(UI::Color4f(0.5f, 0.45f, 0.65f, 1.f), counter);
    }

    dl.AddLine(
        {hdrMin.x + HeaderPadding, hdrMax.y - 1.f}, {hdrMin.x + panelW - HeaderPadding, hdrMax.y - 1.f},
        UI::MakeColor32(80, 55, 120, 200)
    );

    ctx.SetCursorPosY(headerH);
}

float Widgets::HeaderButtonX(const float panelW, const int slot, const float buttonW)
{
    return panelW - PopReserve - HeaderPadding - buttonW - static_cast<float>(slot) * (buttonW + HeaderBtnGap);
}