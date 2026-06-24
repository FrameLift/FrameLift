// ReSharper disable CppMemberFunctionMayBeStatic
#include "UIContextImpl.h"
#include "imgui.h"
#include "IGraphicsBackend.h"
#include <framelift/Hotkeys.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// ── Internal conversion helpers ───────────────────────────────────────────────
namespace
{
ImVec2 V(UI::Vec2 v)
{
    return {v.x, v.y};
}

ImVec4 V(UI::Color4f c)
{
    return {c.r, c.g, c.b, c.a};
}

ImGuiCond ToImGuiCond(UI::Cond c)
{
    return c == UI::Cond::FirstUseEver ? ImGuiCond_FirstUseEver : ImGuiCond_Always;
}

ImGuiStyleVar ToImGuiStyleVar(UI::StyleVar v)
{
    return v == UI::StyleVar::WindowRounding ? ImGuiStyleVar_WindowRounding : ImGuiStyleVar_WindowPadding;
}

ImGuiCol ToImGuiCol(UI::ColorSlot s)
{
    switch (s)
    {
    case UI::ColorSlot::Text:
        return ImGuiCol_Text;
    case UI::ColorSlot::WindowBg:
        return ImGuiCol_WindowBg;
    case UI::ColorSlot::Border:
        return ImGuiCol_Border;
    case UI::ColorSlot::ChildBg:
        return ImGuiCol_ChildBg;
    case UI::ColorSlot::Button:
        return ImGuiCol_Button;
    case UI::ColorSlot::ButtonHovered:
        return ImGuiCol_ButtonHovered;
    case UI::ColorSlot::ButtonActive:
        return ImGuiCol_ButtonActive;
    }
    return ImGuiCol_Text;
}

ImGuiWindowFlags ToImGuiWindowFlags(UI::WindowFlags f)
{
    return static_cast<int>(f);
}

ImGuiSelectableFlags ToImGuiSelectableFlags(UI::SelectableFlags f)
{
    return static_cast<int>(f);
}
} // namespace

// ── DrawListImpl ──────────────────────────────────────────────────────────────

// Convert a window-relative point to the draw list's screen space.
ImVec2 DrawListImpl::P(const UI::Vec2 v) const noexcept
{
    return {v.x + offX_, v.y + offY_};
}

void DrawListImpl::AddRectFilled(UI::Vec2 min, UI::Vec2 max, UI::Color32 col, float r) const noexcept
{
    if (dl_)
    {
        dl_->AddRectFilled(P(min), P(max), col, r);
    }
}

void DrawListImpl::AddLine(UI::Vec2 p1, UI::Vec2 p2, UI::Color32 col) const noexcept
{
    if (dl_)
    {
        dl_->AddLine(P(p1), P(p2), col);
    }
}

void DrawListImpl::AddRectFilledMultiColor(
    UI::Vec2 min, UI::Vec2 max, UI::Color32 cUL, UI::Color32 cUR, UI::Color32 cDR, UI::Color32 cDL
) const noexcept
{
    if (dl_)
    {
        dl_->AddRectFilledMultiColor(P(min), P(max), cUL, cUR, cDR, cDL);
    }
}

void DrawListImpl::AddTriangleFilled(UI::Vec2 p1, UI::Vec2 p2, UI::Vec2 p3, UI::Color32 col) const noexcept
{
    if (dl_)
    {
        dl_->AddTriangleFilled(P(p1), P(p2), P(p3), col);
    }
}

void DrawListImpl::AddText(UI::Vec2 pos, UI::Color32 col, const char* text) const noexcept
{
    if (dl_)
    {
        dl_->AddText(P(pos), col, text);
    }
}

void DrawListImpl::AddImage(uintptr_t handle, UI::Vec2 min, UI::Vec2 max, UI::Color32 tint) const noexcept
{
    if (dl_)
    {
        // ImGui 1.92+ takes ImTextureRef; it has an implicit ctor from ImTextureID.
        dl_->AddImage(static_cast<ImTextureID>(handle), P(min), P(max), ImVec2(0, 0), ImVec2(1, 1), tint);
    }
}

void DrawListImpl::AddCircleFilled(UI::Vec2 center, float radius, UI::Color32 col) const noexcept
{
    if (dl_)
    {
        dl_->AddCircleFilled(P(center), radius, col);
    }
}

// ── UIContextImpl ─────────────────────────────────────────────────────────────

UIContextImpl::UIContextImpl(const Hotkeys* keys) : keys_(keys)
{
}

void UIContextImpl::BeginFrame() noexcept
{
    // A fresh frame starts with no pending redraw demand; renderables re-assert it via
    // RequestRedraw() while they still animate / show live data (see ConsumeRedrawRequest).
    redrawRequested_ = false;

    // Draw list pointers are refreshed lazily on each Get*DrawList() call.
    windowDL_.SetTarget(nullptr);
    backgroundDL_.SetTarget(nullptr);
    foregroundDL_.SetTarget(nullptr);

    // Cache the main viewport (OS window) screen position and size for this frame.
    // Position maps window-relative coordinates to/from screen space (zero unless
    // multi-viewport is enabled); size replaces the old Render(windowW, windowH).
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    mainPosX_ = vp->Pos.x;
    mainPosY_ = vp->Pos.y;
    mainW_ = vp->Size.x;
    mainH_ = vp->Size.y;
}

float UIContextImpl::GetDeltaTime() const noexcept
{
    return ImGui::GetIO().DeltaTime;
}

bool UIContextImpl::ConsumeRedrawRequest() noexcept
{
    // A plugin asked for another frame, OR ImGui itself wants to keep animating: an active
    // item (a slider being dragged, a button held) or a focused text field (the caret blinks
    // and the selection animates). Honouring these here means the loop keeps painting through
    // continuous interaction without the host knowing which plugin owns the widget. Must be
    // called while the ImGui frame is still live (items submitted, before ImGui::Render()).
    const ImGuiIO& io = ImGui::GetIO();
    const bool want = redrawRequested_ || io.WantTextInput || ImGui::IsAnyItemActive();
    redrawRequested_ = false;
    return want;
}

void UIContextImpl::SetNextWindowPos(UI::Vec2 pos, UI::Cond cond) noexcept
{
    // Incoming pos is main-window-relative; map it to screen space.
    ImGui::SetNextWindowPos(ImVec2(pos.x + mainPosX_, pos.y + mainPosY_), ToImGuiCond(cond));
}

void UIContextImpl::SetNextWindowSize(UI::Vec2 size, UI::Cond cond) noexcept
{
    ImGui::SetNextWindowSize(V(size), ToImGuiCond(cond));
}

void UIContextImpl::SetNextWindowBgAlpha(float alpha) noexcept
{
    ImGui::SetNextWindowBgAlpha(alpha);
}

bool UIContextImpl::Begin(const char* name, bool* open, UI::WindowFlags flags) noexcept
{
    return ImGui::Begin(name, open, ToImGuiWindowFlags(flags));
}

void UIContextImpl::End() noexcept
{
    ImGui::End();
}

bool UIContextImpl::BeginChild(const char* id, UI::Vec2 size) noexcept
{
    return ImGui::BeginChild(id, V(size));
}

void UIContextImpl::EndChild() noexcept
{
    ImGui::EndChild();
}

void UIContextImpl::PushStyleVar(UI::StyleVar var, float val) noexcept
{
    ImGui::PushStyleVar(ToImGuiStyleVar(var), val);
}

void UIContextImpl::PushStyleVar(UI::StyleVar var, UI::Vec2 val) noexcept
{
    ImGui::PushStyleVar(ToImGuiStyleVar(var), V(val));
}

void UIContextImpl::PopStyleVar(int count) noexcept
{
    ImGui::PopStyleVar(count);
}

void UIContextImpl::PushStyleColor(UI::ColorSlot slot, UI::Color4f col) noexcept
{
    ImGui::PushStyleColor(ToImGuiCol(slot), V(col));
}

void UIContextImpl::PopStyleColor(int count) noexcept
{
    ImGui::PopStyleColor(count);
}

UI::Vec2 UIContextImpl::GetCursorScreenPos() const noexcept
{
    const auto p = ImGui::GetCursorScreenPos();
    return {p.x, p.y};
}

UI::Vec2 UIContextImpl::GetWindowPos() const noexcept
{
    const auto p = ImGui::GetWindowPos();
    return {p.x, p.y};
}

float UIContextImpl::GetWindowWidth() const noexcept
{
    return ImGui::GetWindowWidth();
}

float UIContextImpl::GetWindowHeight() const noexcept
{
    return ImGui::GetWindowHeight();
}

UI::Vec2 UIContextImpl::GetMainWindowScreenPos() const noexcept
{
    return {mainPosX_, mainPosY_};
}

UI::Vec2 UIContextImpl::GetMainWindowSize() const noexcept
{
    return {mainW_, mainH_};
}

void UIContextImpl::PinNextWindowToMainViewport() noexcept
{
    ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
}

void UIContextImpl::SetCursorPosX(float x) noexcept
{
    ImGui::SetCursorPosX(x);
}

void UIContextImpl::SetCursorPosY(float y) noexcept
{
    ImGui::SetCursorPosY(y);
}

void UIContextImpl::SameLine() noexcept
{
    ImGui::SameLine();
}

void UIContextImpl::PushID(int id) noexcept
{
    ImGui::PushID(id);
}

void UIContextImpl::PushID(const char* s) noexcept
{
    ImGui::PushID(s);
}

void UIContextImpl::PopID() noexcept
{
    ImGui::PopID();
}

bool UIContextImpl::Selectable(const char* label, bool selected, UI::SelectableFlags flags, UI::Vec2 size) noexcept
{
    return ImGui::Selectable(label, selected, ToImGuiSelectableFlags(flags), V(size));
}

void UIContextImpl::Text(const char* text) noexcept
{
    ImGui::TextUnformatted(text);
}

void UIContextImpl::TextWrapped(const char* text) noexcept
{
    ImGui::PushTextWrapPos(0.0f); // wrap to the window content region's right edge
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
}

void UIContextImpl::TextColored(UI::Color4f col, const char* text) noexcept
{
    ImGui::PushStyleColor(ImGuiCol_Text, V(col));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

bool UIContextImpl::Checkbox(const char* label, bool* v) noexcept
{
    return ImGui::Checkbox(label, v);
}

bool UIContextImpl::Button(const char* label, UI::Vec2 size) noexcept
{
    return ImGui::Button(label, V(size));
}

bool UIContextImpl::SliderInt(const char* label, int* v, int min, int max) noexcept
{
    return ImGui::SliderInt(label, v, min, max);
}

bool UIContextImpl::SliderFloat(const char* label, float* v, float min, float max) noexcept
{
    return ImGui::SliderFloat(label, v, min, max);
}

bool UIContextImpl::InputText(const char* label, char* buf, int bufSize) noexcept
{
    return ImGui::InputText(label, buf, static_cast<size_t>(bufSize));
}

bool UIContextImpl::InputTextWithHint(const char* label, const char* hint, char* buf, int bufSize) noexcept
{
    return ImGui::InputTextWithHint(label, hint, buf, static_cast<size_t>(bufSize));
}

bool UIContextImpl::InputTextMultiline(const char* label, char* buf, int bufSize, UI::Vec2 size) noexcept
{
    return ImGui::InputTextMultiline(label, buf, static_cast<size_t>(bufSize), ImVec2(size.x, size.y));
}

void UIContextImpl::Separator() noexcept
{
    ImGui::Separator();
}

void UIContextImpl::SetNextItemWidth(float w) noexcept
{
    ImGui::SetNextItemWidth(w);
}

void UIContextImpl::PushItemWidth(float w) noexcept
{
    ImGui::PushItemWidth(w);
}

void UIContextImpl::PopItemWidth() noexcept
{
    ImGui::PopItemWidth();
}

void UIContextImpl::Dummy(UI::Vec2 size) noexcept
{
    ImGui::Dummy({size.x, size.y});
}

bool UIContextImpl::IsItemDeactivatedAfterEdit() const noexcept
{
    return ImGui::IsItemDeactivatedAfterEdit();
}

bool UIContextImpl::IsItemHovered() const noexcept
{
    return ImGui::IsItemHovered();
}

bool UIContextImpl::BeginTooltip() noexcept
{
    return ImGui::BeginTooltip();
}

void UIContextImpl::EndTooltip() noexcept
{
    ImGui::EndTooltip();
}

void UIContextImpl::SeparatorLine(UI::Color32 color, float inset) noexcept
{
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float avail = ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x + inset, p.y), ImVec2(p.x + avail - inset, p.y), color);
    ImGui::Dummy(ImVec2(0.f, 1.f));
}

bool UIContextImpl::BeginPopupContextVoid(const char* id) noexcept
{
    return ImGui::BeginPopupContextVoid(id);
}

void UIContextImpl::OpenPopup(const char* id) noexcept
{
    ImGui::OpenPopup(id);
}

bool UIContextImpl::BeginPopupModal(const char* id) noexcept
{
    return ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_AlwaysAutoResize);
}

void UIContextImpl::CloseCurrentPopup() noexcept
{
    ImGui::CloseCurrentPopup();
}

void UIContextImpl::EndPopup() noexcept
{
    ImGui::EndPopup();
}

bool UIContextImpl::MenuItem(const char* label) noexcept
{
    return ImGui::MenuItem(label);
}

bool UIContextImpl::MenuItem(const char* label, bool checked) noexcept
{
    return ImGui::MenuItem(label, nullptr, checked);
}

bool UIContextImpl::MenuItem(const char* label, const char* shortcut) noexcept
{
    if (shortcut && keys_)
    {
        char buf[64] = {};
        if (keys_->GetShortcutString(shortcut, buf, sizeof(buf)) > 0)
        {
            return ImGui::MenuItem(label, buf);
        }
    }
    return ImGui::MenuItem(label, shortcut);
}

bool UIContextImpl::MenuItem(const char* label, const char* shortcut, bool checked) noexcept
{
    if (shortcut && keys_)
    {
        char buf[64] = {};
        if (keys_->GetShortcutString(shortcut, buf, sizeof(buf)) > 0)
        {
            return ImGui::MenuItem(label, buf, checked);
        }
    }
    return ImGui::MenuItem(label, shortcut, checked);
}

void UIContextImpl::TextDisabled(const char* text) noexcept
{
    ImGui::TextDisabled("%s", text);
}

void UIContextImpl::TextDisabledWrapped(const char* text) noexcept
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::PushTextWrapPos(0.0f); // wrap to the window content region's right edge
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

bool UIContextImpl::BeginMenu(const char* label) noexcept
{
    return ImGui::BeginMenu(label);
}

void UIContextImpl::EndMenu() noexcept
{
    ImGui::EndMenu();
}

DrawList& UIContextImpl::GetWindowDrawList() noexcept
{
    // Window draw lists already operate in the current window's screen space, and
    // panel code feeds them screen coordinates from GetCursorScreenPos() — no offset.
    windowDL_.SetOffset(0.f, 0.f);
    windowDL_.SetTarget(ImGui::GetWindowDrawList());
    return windowDL_;
}

DrawList& UIContextImpl::GetBackgroundDrawList() noexcept
{
    // Background/foreground draw lists belong to the main viewport's screen space;
    // shift window-relative plugin coordinates onto the main window.
    backgroundDL_.SetOffset(mainPosX_, mainPosY_);
    backgroundDL_.SetTarget(ImGui::GetBackgroundDrawList());
    return backgroundDL_;
}

DrawList& UIContextImpl::GetForegroundDrawList() noexcept
{
    foregroundDL_.SetOffset(mainPosX_, mainPosY_);
    foregroundDL_.SetTarget(ImGui::GetForegroundDrawList());
    return foregroundDL_;
}

// ── Texture loading ───────────────────────────────────────────────────────────
// Pixel upload is delegated to the active graphics backend so the returned ImGui
// texture handle is backend-correct (GL texture name vs Vulkan descriptor set).

uintptr_t UIContextImpl::LoadTexture(const char* path) noexcept
{
    if (!path || !backend_)
    {
        return 0;
    }
    const auto it = textureCache_.find(path);
    if (it != textureCache_.end())
    {
        return it->second;
    }
    int w = 0, h = 0, ch = 0;
    unsigned char* pixels = stbi_load(path, &w, &h, &ch, 4);
    if (!pixels)
    {
        return 0;
    }
    const uintptr_t handle = backend_->CreateUiTexture(pixels, w, h);
    stbi_image_free(pixels);
    textureCache_[path] = handle;
    return handle;
}

uintptr_t UIContextImpl::LoadTextureFromMemory(const unsigned char* data, int size) noexcept
{
    if (!data || size <= 0 || !backend_)
    {
        return 0;
    }
    int w = 0, h = 0, ch = 0;
    unsigned char* pixels = stbi_load_from_memory(data, size, &w, &h, &ch, 4);
    if (!pixels)
    {
        return 0;
    }
    const uintptr_t handle = backend_->CreateUiTexture(pixels, w, h);
    stbi_image_free(pixels);
    return handle;
}

UI::Vec2 UIContextImpl::GetMousePos() const noexcept
{
    // Report main-window-relative coordinates to match plugin HUD hit-testing.
    const auto p = ImGui::GetIO().MousePos;
    return {p.x - mainPosX_, p.y - mainPosY_};
}

bool UIContextImpl::IsMouseClicked(int button) const noexcept
{
    return ImGui::IsMouseClicked(button);
}

bool UIContextImpl::IsMouseDown(int button) const noexcept
{
    return ImGui::IsMouseDown(button);
}

bool UIContextImpl::BeginCombo(const char* label, const char* previewValue) noexcept
{
    return ImGui::BeginCombo(label, previewValue);
}

void UIContextImpl::EndCombo() noexcept
{
    ImGui::EndCombo();
}

bool UIContextImpl::ColorEdit3(const char* label, float* rgb) noexcept
{
    return ImGui::ColorEdit3(label, rgb);
}
