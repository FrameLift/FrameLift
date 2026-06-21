#pragma once

#include <framelift/core.h>
#include <framelift/platform.h>
#include <framelift/ui.h>

#include <string>
#include <vector>

// Right-click context menu for the main video area, shipped as a plugin.
//
// Owns the menu container, renders it (renderOrder 30), and registers itself as
// the ABI `ContextMenu` service that other plugins extend via AddSection(). The
// core playback actions (Open File, Play/Pause, Fullscreen, Audio, Subtitles,
// Quit) are assembled from host services on the first frame — by then every other
// plugin has installed and registered its section, so the final order is
// host core items → plugin sections → Quit, matching the former host-built menu.
class ContextMenuModule final : public ModuleBase, public SafeRenderable, public ContextMenu
{
public:
    // ── ContextMenu service ABI ────────────────────────────────────────────────
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

protected:
    const char* ModuleName() const override
    {
        return "ContextMenu";
    }

    void OnInstall(IModuleContext& ctx) override;
    void HandleMediaEvent(const MediaEvent& e) override;
    void HandleShutdown() override;
    void OnRender(UIContext& ctx) override;

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

    // Invoke every registered section builder in order, appending their items at
    // the current position — called once during Assemble(), between the core
    // items and Quit. Internal: not on the ContextMenu ABI.
    void EmitSections();
    void RenderItems(UIContext& ctx, std::vector<Item>& items);

    // First-frame assembly: host core items, then plugin sections, then Quit.
    void Assemble();
    void BuildCoreItems();

    // ── Actions ported from the former host App::BuildContextMenu() ─────────────
    void OpenFileAction();
    void TogglePauseAction();
    AudioNormalizeParams NormalizeParams() const;

    std::vector<Item> items_;
    std::vector<Section> sections_;
    Hotkeys* keys_ = nullptr;

    IMediaPlayback* playback_ = nullptr;     // transport (pause)
    IMediaProperties* props_ = nullptr;      // idle-state observation
    IAudioControl* audio_ = nullptr;         // mute / normalize / device / track
    ISubtitleControl* subtitles_ = nullptr;  // subtitle toggle / track
    IAppWindow* appWindow_ = nullptr;
    IEventPump* events_ = nullptr;
    IFileDialog* fileDialog_ = nullptr;

    bool assembled_ = false;
    bool playerIdle_ = true;
};

FRAMELIFT_MODULE_ENTRY(ContextMenuModule, {
    .renderOrder = 30,
})
