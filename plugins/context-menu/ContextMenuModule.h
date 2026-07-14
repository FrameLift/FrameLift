#pragma once

#include <framelift/core.h>
#include <framelift/platform.h>

#include <QtCore/QObject>
#include <QtCore/QVariantList>
#include <QtCore/QVariantMap>
#include <string>
#include <vector>

// Right-click context menu for the main video area, shipped as a plugin.
//
// Owns the menu container, exposes it to QML, and registers itself as
// the ABI `ContextMenu` service that other plugins extend via AddSection(). The
// core playback actions (Open File, Play/Pause, Fullscreen, Audio, Subtitles,
// Quit) live in QML. Peer plugin sections are assembled lazily, so the final order is
// host core items → plugin sections → Quit, matching the former host-built menu.
class ContextMenuModule final : public QObject,
                                public ModuleBase,
                                public ContextMenu,
                                public IContextMenuSubmenuRegistry
{
    Q_OBJECT
    Q_PROPERTY(QVariantList extraItems READ QmlExtraItems NOTIFY menuChanged)
    Q_PROPERTY(bool muted READ Muted NOTIFY menuChanged)
    Q_PROPERTY(bool normalizeEnabled READ NormalizeEnabled NOTIFY menuChanged)
    Q_PROPERTY(bool subtitlesEnabled READ SubtitlesEnabled NOTIFY menuChanged)
    Q_PROPERTY(QVariantList audioTracks READ QmlAudioTracks NOTIFY tracksChanged)
    Q_PROPERTY(QVariantList subtitleTracks READ QmlSubtitleTracks NOTIFY tracksChanged)
    // Resolved keyboard shortcuts for the host's core actions, keyed by hotkey id
    // (e.g. "togglePause") so QML can show them next to the built-in menu items.
    Q_PROPERTY(QVariantMap coreShortcuts READ QmlCoreShortcuts NOTIFY menuChanged)

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

    // ── IContextMenuSubmenuRegistry service ABI ──────────────────────────────
    void AddSubmenuSectionRaw(
        const char* label, void (*builder)(ContextMenu&, void*), void* ud, void (*cleanup)(void*)
    ) noexcept override;

    [[nodiscard]] QVariantList QmlExtraItems();

    // Live audio / subtitle track lists for the menu's submenus. Each row is a
    // {id, label, selected} map; rebuilt from the player on every read (the lists
    // are small and only the open submenu binds to them).
    [[nodiscard]] QVariantList QmlAudioTracks() const;
    [[nodiscard]] QVariantList QmlSubtitleTracks() const;
    [[nodiscard]] QVariantMap QmlCoreShortcuts() const;

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
    Q_INVOKABLE void selectAudioTrack(qint64 id);
    Q_INVOKABLE void selectSubtitleTrack(qint64 id);
    Q_INVOKABLE void invokeExtra(int actionId);
    Q_INVOKABLE void quit();

Q_SIGNALS:
    void menuChanged();
    void tracksChanged();

protected:
    const char* ModuleName() const override
    {
        return "ContextMenu";
    }

    void OnInstall(IModuleContext& ctx) override;
    void HandleMediaEvent(const MediaEvent& e) override;
    void HandleShutdown() override;

private:
    friend class ContextMenuTest;

    struct Item
    {
        std::string label;
        std::string hotkeyName;
        void (*action)(void*) = nullptr;
        void* ud = nullptr;
        void (*cleanup)(void*) = nullptr;
        std::vector<Item> children;
        int actionId = -1; // stable QML invocation id, including nested actions
        bool core = false; // host playback action (rendered by QML, excluded from extraItems)
    };

    struct Section
    {
        std::string submenuLabel; // empty for an ordinary flat section
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
    [[nodiscard]] std::vector<Item>& CurrentItems();
    [[nodiscard]] QVariantList ProjectItems(const std::vector<Item>& items, bool topLevel) const;
    [[nodiscard]] static Item* FindAction(std::vector<Item>& items, int actionId);
    static void CleanupItems(std::vector<Item>& items);

    // ── Actions ported from the former host App::BuildContextMenu() ─────────────
    void OpenFileAction();
    void TogglePauseAction();
    AudioNormalizeParams NormalizeParams() const;
    [[nodiscard]] std::string ShortcutFor(const std::string& hotkeyName) const;

    std::vector<Item> items_;
    std::vector<Section> sections_;
    std::vector<Item>* buildingItems_ = nullptr;
    int nextActionId_ = 0;
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
    // True while BuildCoreItems()/Assemble() append the host playback actions, so
    // AddItem*Raw can stamp Item::core — robustly distinguishing core items from
    // peer-plugin sections without matching label strings.
    bool buildingCore_ = false;

    // ── QML extra-items cache ──────────────────────────────────────────────────
    // QmlExtraItems() filters items_ and rebuilds a QVariantList on every read.
    // Cache it and invalidate whenever menuChanged fires (the NOTIFY for the
    // `extraItems` property).
    QVariantList extraItemsCache_;
    bool extraItemsCacheDirty_ = true;
};

FRAMELIFT_MODULE_ENTRY(
    ContextMenuModule, {
                           .renderOrder = 30,
                       }
)
