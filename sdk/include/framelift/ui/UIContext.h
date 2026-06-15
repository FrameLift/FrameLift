#pragma once
#include <cstddef>
#include <framelift/ui/UI.h>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>
#include <string>

class Hotkeys;

// ── DrawList ──────────────────────────────────────────────────────────────────
// Abstract draw-command list. Obtained from UIContext::Get*DrawList().
// Returned by reference — the reference is valid for the duration of the
// current Render() call. All methods are noexcept.
class DrawList
{
public:
    static constexpr const char* InterfaceId = "framelift.DrawList";
    virtual ~DrawList() = default;

    virtual void AddRectFilled(UI::Vec2 min, UI::Vec2 max, UI::Color32 col, float rounding = 0.f) const noexcept = 0;
    virtual void AddLine(UI::Vec2 p1, UI::Vec2 p2, UI::Color32 col) const noexcept = 0;
    virtual void AddRectFilledMultiColor(
        UI::Vec2 min, UI::Vec2 max, UI::Color32 colUL, UI::Color32 colUR, UI::Color32 colDR, UI::Color32 colDL
    ) const noexcept = 0;
    virtual void AddTriangleFilled(UI::Vec2 p1, UI::Vec2 p2, UI::Vec2 p3, UI::Color32 col) const noexcept = 0;
    virtual void AddText(UI::Vec2 pos, UI::Color32 col, const char* text) const noexcept = 0;
    virtual void AddImage(
        uintptr_t textureHandle, UI::Vec2 min, UI::Vec2 max, UI::Color32 tint = UI::MakeColor32(255, 255, 255, 255)
    ) const noexcept = 0;
    virtual void AddCircleFilled(UI::Vec2 center, float radius, UI::Color32 col) const noexcept = 0;
};

// ── UIContext ─────────────────────────────────────────────────────────────────
// Abstract facade over Dear ImGui. Implemented by the host; passed into every
// IRenderable::Render() call. No imgui.h symbols are visible to plugins.
class UIContext
{
public:
    static constexpr const char* InterfaceId = "framelift.UIContext";
    virtual ~UIContext() = default;

    // ── Timing ────────────────────────────────────────────────────────────────
    [[nodiscard]] virtual float GetDeltaTime() const noexcept = 0;

    // ── Window management ─────────────────────────────────────────────────────
    virtual void SetNextWindowPos(UI::Vec2 pos, UI::Cond cond = UI::Cond::Always) noexcept = 0;
    virtual void SetNextWindowSize(UI::Vec2 size, UI::Cond cond = UI::Cond::Always) noexcept = 0;
    virtual void SetNextWindowBgAlpha(float alpha) noexcept = 0;
    virtual bool Begin(const char* name, bool* open, UI::WindowFlags flags) noexcept = 0;
    virtual void End() noexcept = 0;
    virtual bool BeginChild(const char* id, UI::Vec2 size) noexcept = 0;
    virtual void EndChild() noexcept = 0;

    // ── Style ─────────────────────────────────────────────────────────────────
    virtual void PushStyleVar(UI::StyleVar var, float val) noexcept = 0;
    virtual void PushStyleVar(UI::StyleVar var, UI::Vec2 val) noexcept = 0;
    virtual void PopStyleVar(int count = 1) noexcept = 0;
    virtual void PushStyleColor(UI::ColorSlot slot, UI::Color4f col) noexcept = 0;
    virtual void PopStyleColor(int count = 1) noexcept = 0;

    // ── Layout & cursor ───────────────────────────────────────────────────────
    [[nodiscard]] virtual UI::Vec2 GetCursorScreenPos() const noexcept = 0;
    [[nodiscard]] virtual UI::Vec2 GetWindowPos() const noexcept = 0;
    [[nodiscard]] virtual float GetWindowWidth() const noexcept = 0;
    virtual void SetCursorPosX(float x) noexcept = 0;
    virtual void SetCursorPosY(float y) noexcept = 0;
    virtual void SameLine() noexcept = 0;

    // ── ID stack ──────────────────────────────────────────────────────────────
    virtual void PushID(int id) noexcept = 0;
    virtual void PushID(const char* strId) noexcept = 0;
    virtual void PopID() noexcept = 0;

    // ── Widgets ───────────────────────────────────────────────────────────────
    virtual bool Selectable(const char* label, bool selected, UI::SelectableFlags flags, UI::Vec2 size) noexcept = 0;
    virtual void Text(const char* text) noexcept = 0;
    virtual void TextColored(UI::Color4f col, const char* text) noexcept = 0;
    virtual bool Checkbox(const char* label, bool* v) noexcept = 0;
    virtual bool Button(const char* label, UI::Vec2 size = {}) noexcept = 0;
    virtual bool SliderInt(const char* label, int* v, int min, int max) noexcept = 0;
    virtual bool SliderFloat(const char* label, float* v, float min, float max) noexcept = 0;
    // POD boundary: caller owns buf (capacity bufSize incl. NUL). Returns true on edit.
    virtual bool InputText(const char* label, char* buf, int bufSize) noexcept = 0;
    virtual bool InputTextWithHint(const char* label, const char* hint, char* buf, int bufSize) noexcept = 0;

    // Plugin-side std::string convenience (non-virtual; compiled into the plugin —
    // does NOT cross the ABI boundary). Bridges to the char*/bufSize virtuals above.
    bool InputText(const char* label, std::string& v, int maxLen = 512) noexcept
    {
        std::string buf(static_cast<std::size_t>(maxLen), '\0');
        v.copy(buf.data(), buf.size() - 1);
        if (InputText(label, buf.data(), maxLen))
        {
            v = buf.c_str();
            return true;
        }
        return false;
    }

    bool InputTextWithHint(const char* label, const char* hint, std::string& v, int maxLen = 512) noexcept
    {
        std::string buf(static_cast<std::size_t>(maxLen), '\0');
        v.copy(buf.data(), buf.size() - 1);
        if (InputTextWithHint(label, hint, buf.data(), maxLen))
        {
            v = buf.c_str();
            return true;
        }
        return false;
    }

    virtual void Separator() noexcept = 0;
    virtual void SetNextItemWidth(float width) noexcept = 0;
    virtual void PushItemWidth(float width) noexcept = 0;
    virtual void PopItemWidth() noexcept = 0;
    virtual void Dummy(UI::Vec2 size) noexcept = 0;

    // ── Item state ────────────────────────────────────────────────────────────
    [[nodiscard]] virtual bool IsItemDeactivatedAfterEdit() const noexcept = 0;
    [[nodiscard]] virtual bool IsItemHovered() const noexcept = 0;

    // ── Tooltip ───────────────────────────────────────────────────────────────
    virtual bool BeginTooltip() noexcept = 0;
    virtual void EndTooltip() noexcept = 0;

    // ── Compound draw helpers ──────────────────────────────────────────────────
    virtual void SeparatorLine(UI::Color32 color, float inset = 0.f) noexcept = 0;

    // ── Popups ────────────────────────────────────────────────────────────────
    virtual bool BeginPopupContextVoid(const char* id = nullptr) noexcept = 0;
    virtual void OpenPopup(const char* id) noexcept = 0;
    virtual bool BeginPopupModal(const char* id) noexcept = 0;
    virtual void CloseCurrentPopup() noexcept = 0;
    virtual void EndPopup() noexcept = 0;
    virtual bool MenuItem(const char* label) noexcept = 0;
    virtual bool MenuItem(const char* label, bool checked) noexcept = 0;
    virtual bool MenuItem(const char* label, const char* shortcut) noexcept = 0;
    virtual bool MenuItem(const char* label, const char* shortcut, bool checked) noexcept = 0;
    virtual void TextDisabled(const char* text) noexcept = 0;
    virtual bool BeginMenu(const char* label) noexcept = 0;
    virtual void EndMenu() noexcept = 0;

    // ── Draw lists ────────────────────────────────────────────────────────────
    // References are valid for the duration of the current Render() call.
    [[nodiscard]] virtual DrawList& GetWindowDrawList() noexcept = 0;
    [[nodiscard]] virtual DrawList& GetBackgroundDrawList() noexcept = 0;
    [[nodiscard]] virtual DrawList& GetForegroundDrawList() noexcept = 0;

    // ── Texture loading (host-managed GPU textures) ───────────────────────────
    // Load from a file path or from memory; returns a handle for AddImage().
    // Returns 0 on failure. The host retains ownership of the texture.
    virtual uintptr_t LoadTexture(const char* path) noexcept = 0;
    virtual uintptr_t LoadTextureFromMemory(const unsigned char* data, int size) noexcept = 0;

    // ── Mouse ─────────────────────────────────────────────────────────────────
    [[nodiscard]] virtual UI::Vec2 GetMousePos() const noexcept = 0;
    [[nodiscard]] virtual bool IsMouseClicked(int button) const noexcept = 0;
    [[nodiscard]] virtual bool IsMouseDown(int button) const noexcept = 0;

    // ── Combo & color ─────────────────────────────────────────────────
    // Combo: open with BeginCombo (previewValue = current selection text). When it
    // returns true, submit items via Selectable() then call EndCombo() exactly once.
    virtual bool BeginCombo(const char* label, const char* previewValue) noexcept = 0;
    virtual void EndCombo() noexcept = 0;
    // ColorEdit3: rgb is an in/out array of 3 floats in 0–1. Returns true while edited.
    virtual bool ColorEdit3(const char* label, float* rgb) noexcept = 0;

    // ── Multiline text ─────────────────────────────────────────────────
    // POD boundary: caller owns buf (capacity bufSize incl. NUL). size is the widget
    // extent in pixels; a zero/negative component follows ImGui's auto-sizing rules
    // (e.g. {0, -36} fills the available width and leaves 36px below). Returns true on edit.
    virtual bool InputTextMultiline(const char* label, char* buf, int bufSize, UI::Vec2 size = {}) noexcept = 0;

    // ── Window geometry ─────────────────────────────────────────────────────────
    // Appended at the end of the interface to preserve the vtable layout for
    // plugins built against an earlier minor. Companion to GetWindowWidth();
    // used by the Panel base when it pops out into its own OS window.
    //
    // All coordinates exposed by this interface are MAIN-WINDOW-RELATIVE (origin
    // at the main window's top-left). The host maps them to/from screen space, so
    // plugins never deal with multi-viewport screen offsets.
    [[nodiscard]] virtual float GetWindowHeight() const noexcept = 0;

    // ── Main window origin ───────────────────────────────────────
    // Screen-space position of the main window's top-left. Companion to
    // IAppWindow::GetDisplayUsableBounds(); lets a popped-out panel convert
    // between main-window-relative and screen coordinates to clamp itself onto
    // the monitor. Zero unless multi-viewport is active.
    [[nodiscard]] virtual UI::Vec2 GetMainWindowScreenPos() const noexcept = 0;

    // Size in pixels of the main OS window's client area. This is what the old
    // Render(windowW, windowH, ...) parameters carried; fetch it on demand instead.
    // Valid during a Render() call (after the host has begun the frame).
    [[nodiscard]] virtual UI::Vec2 GetMainWindowSize() const noexcept = 0;

    // Pin the next window to the main OS window's viewport so it never detaches
    // into its own platform window when positioned past the main window's edge —
    // e.g. a docked panel sliding off-screen during its close animation. Such a
    // window is clipped at the host window's bounds instead. No-op without
    // multi-viewport.
    virtual void PinNextWindowToMainViewport() noexcept = 0;

    // ── Wrapped text ─────────────────────────────────────────────────────────────
    // Like Text(), but wraps long lines to the window's content region instead of
    // clipping at the right edge. Used for multi-line descriptions/help text.
    // Appended at the end to preserve the vtable layout for plugins built against
    // an earlier minor.
    virtual void TextWrapped(const char* text) noexcept = 0;

    // Like TextDisabled(), but wraps long lines to the window's content region.
    virtual void TextDisabledWrapped(const char* text) noexcept = 0;
};
