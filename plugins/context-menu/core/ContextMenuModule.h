#pragma once

#include <framelift/core.h>
#include <framelift/platform.h>

#include <QtCore/QObject>
#include <QtCore/QVariantList>
#include <string>
#include <vector>

// Right-click context menu for the main video area, shipped as a plugin.
//
// Owns the menu container, exposes it to QML, and registers itself as
// the ABI `ContextMenu` service that other plugins extend via AddSection(). The
// core playback actions (Open File, Play/Pause, Fullscreen, Audio, Subtitles,
// Quit) live in QML. Peer plugin sections are assembled lazily, so the final order is
// host core items → plugin sections → Quit, matching the former host-built menu.
class ContextMenuModule final : public QObject, public ModuleBase, public ContextMenu
{
    Q_OBJECT
    Q_PROPERTY(QVariantList extraItems READ QmlExtraItems NOTIFY menuChanged)
    Q_PROPERTY(bool muted READ Muted NOTIFY menuChanged)
    Q_PROPERTY(bool normalizeEnabled READ NormalizeEnabled NOTIFY menuChanged)
    Q_PROPERTY(bool subtitlesEnabled READ SubtitlesEnabled NOTIFY menuChanged)

public:
    // ── ContextMenu service ABI ────────────────────────────────────────────────
    void AddItemRaw(const char* label, void (*action)(void*), void* ud, void (*cleanup)(void*)) noexcept override;

    void AddItemWithHotkeyRaw(
        const char* label, const char* hotkeyName, void (*action)(void*), void* ud, void (*cleanup)(void*)
    ) noexcept override;

    void AddSeparator() noexcept override;

    void Clear() noexcept override;

    void SetKeys(Hotkeys* keys) noexcept override
    {
        keys_ = keys;
    }

    void AddSectionRaw(void (*builder)(ContextMenu&, void*), void* ud, void (*cleanup)(void*)) noexcept override;

    [[nodiscard]] QVariantList QmlExtraItems();

    [[nodiscard]] bool Muted() const
    {
        return audio_ && audio_->IsMuted();
    }

    [[nodiscard]] bool NormalizeEnabled() const
    {
        return audio_ && audio_->IsNormalizeEnabled();
    }

    [[nodiscard]] bool SubtitlesEnabled() const
    {
        return subtitles_ && subtitles_->IsSubtitlesEnabled();
    }

    Q_INVOKABLE void openFile()
    {
        OpenFileAction();
    }

    Q_INVOKABLE void openNetwork();

    Q_INVOKABLE void togglePause()
    {
        TogglePauseAction();
    }

    Q_INVOKABLE void toggleFullscreen();
    Q_INVOKABLE void toggleMute();
    Q_INVOKABLE void toggleNormalize();
    Q_INVOKABLE void toggleSubtitles();
    Q_INVOKABLE void invokeExtra(int index);
    Q_INVOKABLE void quit();

Q_SIGNALS:
    void menuChanged();

protected:
    const char* ModuleName() const override
    {
        return "ContextMenu";
    }

    void OnInstall(IModuleContext& ctx) override;
    void HandleMediaEvent(const MediaEvent& e) override;
    void HandleShutdown() override;

private:
    struct Item
    {
        std::string label;
        std::string hotkeyName;
        void (*action)(void*) = nullptr;
        void* ud = nullptr;
        void (*cleanup)(void*) = nullptr;
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

    IMediaPlayback* playback_ = nullptr;    // transport (pause)
    IMediaProperties* props_ = nullptr;     // idle-state observation
    IAudioControl* audio_ = nullptr;        // mute / normalize / device / track
    ISubtitleControl* subtitles_ = nullptr; // subtitle toggle / track
    IAppWindow* appWindow_ = nullptr;
    IEventPump* events_ = nullptr;
    IFileDialog* fileDialog_ = nullptr;

    bool assembled_ = false;
    bool playerIdle_ = true;
};

FRAMELIFT_MODULE_ENTRY(
    ContextMenuModule, {
                           .renderOrder = 30,
                       }
)
