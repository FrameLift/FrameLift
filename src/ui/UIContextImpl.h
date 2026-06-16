#pragma once
#include <framelift/ui/UIContext.h>
#include <memory>
#include <string>
#include <unordered_map>

struct ImDrawList;
struct ImVec2;
class Hotkeys;
class IGraphicsBackend;

// Concrete draw-list wrapper backed by an ImDrawList*.
// Three instances are held per UIContextImpl (window, background, foreground).
class DrawListImpl final : public DrawList
{
public:
    void SetTarget(ImDrawList* dl) noexcept
    {
        dl_ = dl;
    }

    // Screen-space offset added to every coordinate. The background/foreground
    // draw lists live in the main viewport's screen space, so the host sets this
    // to the main viewport position to keep plugin coordinates window-relative.
    void SetOffset(float x, float y) noexcept
    {
        offX_ = x;
        offY_ = y;
    }

    void AddRectFilled(UI::Vec2 min, UI::Vec2 max, UI::Color32 col, float rounding) const noexcept override;
    void AddLine(UI::Vec2 p1, UI::Vec2 p2, UI::Color32 col) const noexcept override;
    void AddRectFilledMultiColor(
        UI::Vec2 min, UI::Vec2 max, UI::Color32 cUL, UI::Color32 cUR, UI::Color32 cDR, UI::Color32 cDL
    ) const noexcept override;
    void AddTriangleFilled(UI::Vec2 p1, UI::Vec2 p2, UI::Vec2 p3, UI::Color32 col) const noexcept override;
    void AddText(UI::Vec2 pos, UI::Color32 col, const char* text) const noexcept override;
    void AddImage(uintptr_t handle, UI::Vec2 min, UI::Vec2 max, UI::Color32 tint) const noexcept override;
    void AddCircleFilled(UI::Vec2 center, float radius, UI::Color32 col) const noexcept override;

private:
    // Apply offX_/offY_ to a window-relative point, yielding screen space.
    [[nodiscard]] ImVec2 P(UI::Vec2 v) const noexcept;

    ImDrawList* dl_ = nullptr;
    float offX_ = 0.f, offY_ = 0.f;
};

// Concrete UIContext implementation backed by Dear ImGui.
// One instance lives in App; its address is stable for the program lifetime.
class UIContextImpl final : public UIContext
{
public:
    explicit UIContextImpl(const Hotkeys* keys = nullptr);

    // Called once per frame to reset cached draw list pointers.
    void BeginFrame() noexcept;

    float GetDeltaTime() const noexcept override;

    void SetNextWindowPos(UI::Vec2 pos, UI::Cond cond) noexcept override;
    void SetNextWindowSize(UI::Vec2 size, UI::Cond cond) noexcept override;
    void SetNextWindowBgAlpha(float alpha) noexcept override;
    bool Begin(const char* name, bool* open, UI::WindowFlags flags) noexcept override;
    void End() noexcept override;
    bool BeginChild(const char* id, UI::Vec2 size) noexcept override;
    void EndChild() noexcept override;

    void PushStyleVar(UI::StyleVar var, float val) noexcept override;
    void PushStyleVar(UI::StyleVar var, UI::Vec2 val) noexcept override;
    void PopStyleVar(int count) noexcept override;
    void PushStyleColor(UI::ColorSlot slot, UI::Color4f col) noexcept override;
    void PopStyleColor(int count) noexcept override;

    UI::Vec2 GetCursorScreenPos() const noexcept override;
    UI::Vec2 GetWindowPos() const noexcept override;
    float GetWindowWidth() const noexcept override;
    float GetWindowHeight() const noexcept override;
    UI::Vec2 GetMainWindowScreenPos() const noexcept override;
    UI::Vec2 GetMainWindowSize() const noexcept override;
    void PinNextWindowToMainViewport() noexcept override;
    void SetCursorPosX(float x) noexcept override;
    void SetCursorPosY(float y) noexcept override;
    void SameLine() noexcept override;
    void PushID(int id) noexcept override;
    void PushID(const char* strId) noexcept override;
    void PopID() noexcept override;

    bool Selectable(const char* label, bool selected, UI::SelectableFlags flags, UI::Vec2 size) noexcept override;
    void Text(const char* text) noexcept override;
    void TextWrapped(const char* text) noexcept override;
    void TextColored(UI::Color4f col, const char* text) noexcept override;
    bool Checkbox(const char* label, bool* v) noexcept override;
    bool Button(const char* label, UI::Vec2 size) noexcept override;
    bool SliderInt(const char* label, int* v, int min, int max) noexcept override;
    bool SliderFloat(const char* label, float* v, float min, float max) noexcept override;
    bool InputText(const char* label, char* buf, int bufSize) noexcept override;
    bool InputTextWithHint(const char* label, const char* hint, char* buf, int bufSize) noexcept override;
    void Separator() noexcept override;
    void SetNextItemWidth(float width) noexcept override;
    void PushItemWidth(float width) noexcept override;
    void PopItemWidth() noexcept override;
    void Dummy(UI::Vec2 size) noexcept override;

    bool IsItemDeactivatedAfterEdit() const noexcept override;
    bool IsItemHovered() const noexcept override;
    bool BeginTooltip() noexcept override;
    void EndTooltip() noexcept override;
    void SeparatorLine(UI::Color32 color, float inset) noexcept override;
    bool BeginPopupContextVoid(const char* id) noexcept override;
    void OpenPopup(const char* id) noexcept override;
    bool BeginPopupModal(const char* id) noexcept override;
    void CloseCurrentPopup() noexcept override;
    void EndPopup() noexcept override;
    bool MenuItem(const char* label) noexcept override;
    bool MenuItem(const char* label, bool checked) noexcept override;
    bool MenuItem(const char* label, const char* shortcut) noexcept override;
    bool MenuItem(const char* label, const char* shortcut, bool checked) noexcept override;
    void TextDisabled(const char* text) noexcept override;
    void TextDisabledWrapped(const char* text) noexcept override;
    bool BeginMenu(const char* label) noexcept override;
    void EndMenu() noexcept override;

    DrawList& GetWindowDrawList() noexcept override;
    DrawList& GetBackgroundDrawList() noexcept override;
    DrawList& GetForegroundDrawList() noexcept override;

    uintptr_t LoadTexture(const char* path) noexcept override;
    uintptr_t LoadTextureFromMemory(const unsigned char* data, int size) noexcept override;

    UI::Vec2 GetMousePos() const noexcept override;
    bool IsMouseClicked(int button) const noexcept override;
    bool IsMouseDown(int button) const noexcept override;

    bool BeginCombo(const char* label, const char* previewValue) noexcept override;
    void EndCombo() noexcept override;
    bool ColorEdit3(const char* label, float* rgb) noexcept override;
    bool InputTextMultiline(const char* label, char* buf, int bufSize, UI::Vec2 size) noexcept override;

    void UpdateKeys(const Hotkeys* keys) noexcept
    {
        keys_ = keys;
    }

    // The active graphics backend, used to create ImGui textures for plugin icons in a
    // backend-correct way (GL texture vs Vulkan descriptor). Set by App after the window
    // (and its backend) exist.
    void SetGraphicsBackend(IGraphicsBackend* backend) noexcept
    {
        backend_ = backend;
    }

private:
    const Hotkeys* keys_ = nullptr;
    IGraphicsBackend* backend_ = nullptr;
    DrawListImpl windowDL_, backgroundDL_, foregroundDL_;
    std::unordered_map<std::string, uintptr_t> textureCache_;
    // Main viewport (OS window) screen position, cached each BeginFrame. With
    // multi-viewport enabled this is non-zero; the host adds it to window-relative
    // coordinates (and subtracts it from mouse position) so plugins stay unaware.
    float mainPosX_ = 0.f, mainPosY_ = 0.f;
    // Main OS window client-area size in pixels, cached each BeginFrame. Replaces
    // the Render(windowW, windowH) parameters.
    float mainW_ = 0.f, mainH_ = 0.f;
};
