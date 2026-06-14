#pragma once
#include <framelift/IRenderable.h>
#include <framelift/ui/ContextMenu.h>
#include <string>
#include <vector>

class UIContext;

class ContextMenuImpl final : public ContextMenu, public IRenderable
{
public:
    void AddItemRaw(const char* label, void (*action)(void*), void* ud, void (*cleanup)(void*)) noexcept override;

    void AddItemWithHotkeyRaw(
        const char* label, const char* hotkeyName, void (*action)(void*), void* ud, void (*cleanup)(void*)
    ) noexcept override;

    void AddSeparator() noexcept override;

    void AddDynamicSubMenuRaw(
        const char* label, void (*builder)(void*, UIContext&), void* ud, void (*cleanup)(void*)
    ) noexcept override;

    void Clear() noexcept override;

    void SetKeys(Hotkeys* keys) noexcept override
    {
        keys_ = keys;
    }

    void AddSectionRaw(void (*builder)(ContextMenu&, void*), void* ud, void (*cleanup)(void*)) noexcept override;

    // Invoke every registered section builder in order, appending their items at the
    // current position. Host-only (not on the ContextMenu ABI); called once while the
    // menu is assembled, between the core items and Quit.
    void EmitSections();

    void Render(int windowW, int windowH, UIContext& ctx) noexcept override;

    [[nodiscard]] bool NeedsRedraw() const noexcept override
    {
        return menuOpen_;
    }

private:
    struct Item
    {
        std::string label;
        std::string hotkeyName;
        void (*action)(void*) = nullptr;
        void* ud = nullptr;
        void (*cleanup)(void*) = nullptr;
        void (*dynamicBuilder)(void*, UIContext&) = nullptr;
        void* builderUd = nullptr;
        std::vector<Item> children; // for static sub-menus (not used by public API)
    };

    struct Section
    {
        void (*builder)(ContextMenu&, void*) = nullptr;
        void* ud = nullptr;
        void (*cleanup)(void*) = nullptr;
    };

    void RenderItems(UIContext& ctx, std::vector<Item>& items);

    std::vector<Item> items_;
    std::vector<Section> sections_;
    Hotkeys* keys_ = nullptr;
    bool menuOpen_ = false;
};