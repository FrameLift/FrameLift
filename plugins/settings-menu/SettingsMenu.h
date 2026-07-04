#pragma once

#include <framelift/core.h>
#include <framelift/platform.h>
#include <framelift/services.h>

#include <QtCore/QObject>
#include <QtCore/QStringList>
#include <QtCore/QVariant>
#include <QtCore/QVariantList>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class EditModel
{
public:
    void Clear();
    bool& Bool(const std::string& key);
    int& Int(const std::string& key);
    float& Float(const std::string& key);
    std::string& Str(const std::string& key);

private:
    std::unordered_map<std::string, bool> bools_;
    std::unordered_map<std::string, int> ints_;
    std::unordered_map<std::string, float> floats_;
    std::unordered_map<std::string, std::string> strings_;
};

class SettingsMenu;

class SettingsSectionPageModel final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString title READ Title CONSTANT)
    Q_PROPERTY(QVariantList fields READ Fields NOTIFY changed)
    Q_PROPERTY(bool dirty READ Dirty NOTIFY changed)

public:
    SettingsSectionPageModel(SettingsMenu& owner, QString id, QString title);

    [[nodiscard]] QString Title() const;
    [[nodiscard]] QVariantList Fields();
    [[nodiscard]] bool Dirty() const;

    // Current draft value for a single "section.key", for hand-authored pages that
    // bind individual fields instead of iterating the generic `fields` list.
    Q_INVOKABLE QVariant fieldValue(const QString& key) const;
    Q_INVOKABLE void setFieldValue(const QString& key, const QVariant& value);
    // Acceleration modes the machine can actually use (for the Playback page's mode
    // combobox), so it never offers e.g. cuda on an Intel GPU. Delegates to the host
    // IVideoDecodeCaps probe via the owning module.
    Q_INVOKABLE QStringList availableHwdecModes() const;
    Q_INVOKABLE void save();
    Q_INVOKABLE void reset();

Q_SIGNALS:
    void changed();

private:
    SettingsMenu& owner_;
    QString id_;
    QString title_;
};

class SettingsPluginsPageModel final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString title READ Title CONSTANT)
    Q_PROPERTY(QVariantList plugins READ Plugins NOTIFY changed)
    Q_PROPERTY(bool dirty READ Dirty NOTIFY changed)

public:
    explicit SettingsPluginsPageModel(SettingsMenu& owner);

    [[nodiscard]] QString Title() const;
    [[nodiscard]] QVariantList Plugins() const;
    [[nodiscard]] bool Dirty() const;

    Q_INVOKABLE void setPluginEnabled(const QString& pluginId, bool enabled);
    Q_INVOKABLE void save();
    Q_INVOKABLE void reset();

Q_SIGNALS:
    void changed();

private:
    SettingsMenu& owner_;
};

// View-model for the Advanced page: a raw editor over the on-disk settings.ini. The
// draft `text` is seeded from the file on load(); save() writes it back and asks the
// host to reload (ISettingsStore::ReloadSettings), so live settings and other pages'
// drafts pick up the hand edits instead of being clobbered by the next Save.
class SettingsAdvancedPageModel final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString title READ Title CONSTANT)
    Q_PROPERTY(QString filePath READ FilePath NOTIFY changed)
    Q_PROPERTY(QString text READ Text NOTIFY changed)
    Q_PROPERTY(bool dirty READ Dirty NOTIFY changed)

public:
    explicit SettingsAdvancedPageModel(SettingsMenu& owner);

    [[nodiscard]] QString Title() const;
    [[nodiscard]] QString FilePath() const;
    [[nodiscard]] QString Text() const;
    [[nodiscard]] bool Dirty() const;

    // Re-read settings.ini into the draft and clear dirty. Named "load" so the
    // owner's ReseedActivePage() hook refreshes it on open / navigation.
    Q_INVOKABLE void load();
    Q_INVOKABLE void setText(const QString& text);
    Q_INVOKABLE void save();
    Q_INVOKABLE void reset();

Q_SIGNALS:
    void changed();

private:
    SettingsMenu& owner_;
    QString text_;
    bool dirty_ = false;
};

// Dedicated view-model for the Keybinds page: core ("Application") keybinds plus one
// group per plugin, each action with two capture slots. Delegates to SettingsMenu,
// which owns the draft state and the conflict/apply logic.
class KeybindsPageModel final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString title READ Title CONSTANT)
    Q_PROPERTY(QVariantList coreEntries READ CoreEntries NOTIFY changed)
    Q_PROPERTY(QVariantList pluginGroups READ PluginGroups NOTIFY changed)
    Q_PROPERTY(bool dirty READ Dirty NOTIFY changed)
    Q_PROPERTY(bool capturing READ Capturing NOTIFY changed)
    Q_PROPERTY(QString capturingAction READ CapturingAction NOTIFY changed)
    Q_PROPERTY(int capturingSlot READ CapturingSlot NOTIFY changed)
    Q_PROPERTY(QString conflict READ Conflict NOTIFY changed)

public:
    explicit KeybindsPageModel(SettingsMenu& owner);

    [[nodiscard]] QString Title() const;
    [[nodiscard]] QVariantList CoreEntries() const;
    [[nodiscard]] QVariantList PluginGroups() const;
    [[nodiscard]] bool Dirty() const;
    [[nodiscard]] bool Capturing() const;
    [[nodiscard]] QString CapturingAction() const;
    [[nodiscard]] int CapturingSlot() const;
    [[nodiscard]] QString Conflict() const;

    Q_INVOKABLE void beginCapture(const QString& action, int slot);
    Q_INVOKABLE void captureKey(int qtKey, int qtMods);
    Q_INVOKABLE void cancelCapture();
    Q_INVOKABLE void clearSlot(const QString& action, int slot);
    Q_INVOKABLE void save();
    Q_INVOKABLE void reset();

Q_SIGNALS:
    void changed();

private:
    SettingsMenu& owner_;
};

class SettingsMenu final : public QObject, public ModuleBase
{
    Q_OBJECT
    Q_PROPERTY(bool open READ IsOpen NOTIFY qmlChanged)
    Q_PROPERTY(bool dirty READ Dirty NOTIFY qmlChanged)
    Q_PROPERTY(QVariantList pages READ QmlPages NOTIFY qmlChanged)
    Q_PROPERTY(QString activePage READ ActivePage WRITE SetActivePage NOTIFY qmlChanged)
    Q_PROPERTY(QString activePageUrl READ ActivePageUrl NOTIFY qmlChanged)
    Q_PROPERTY(QObject* activePageViewModel READ ActivePageViewModel NOTIFY qmlChanged)

public:
    bool HandleKeyDownEvent(const AppEvent& e) override;

    void Open() noexcept;
    void OpenPage(const char* pageId) noexcept;
    void Close() noexcept;

    [[nodiscard]] bool IsOpen() const noexcept;
    [[nodiscard]] bool Dirty() const noexcept;
    [[nodiscard]] QVariantList QmlPages();
    [[nodiscard]] QString ActivePage() const;
    void SetActivePage(const QString& page);
    [[nodiscard]] QString ActivePageUrl() const;
    [[nodiscard]] QObject* ActivePageViewModel() const;

    Q_INVOKABLE QVariantList fieldsForPage(const QString& pageId);
    Q_INVOKABLE QVariantList pluginsModel() const;
    Q_INVOKABLE void setFieldValue(const QString& key, const QVariant& value);
    Q_INVOKABLE void setPluginEnabled(const QString& pluginId, bool enabled);
    Q_INVOKABLE void saveActivePage();
    Q_INVOKABLE void resetActivePage();
    // Reset only the active page's scope (its section's fields, or — for the Advanced
    // and Plugins pages — that page's own reset semantics). Distinct from resetAllQml.
    Q_INVOKABLE void resetActivePageOnly();
    Q_INVOKABLE void saveQml();
    Q_INVOKABLE void resetAllQml();
    Q_INVOKABLE void closeQml();

    [[nodiscard]] bool SettingBool(const std::string& key);
    [[nodiscard]] int SettingInt(const std::string& key);
    [[nodiscard]] float SettingFloat(const std::string& key);
    [[nodiscard]] std::string SettingString(const std::string& key);

    // ── Advanced (raw settings.ini) page support ─────────────────────────────────
    // Absolute path of the on-disk settings.ini (empty if the store is unavailable).
    [[nodiscard]] std::string SettingsFilePath() const;
    // Re-read settings.ini from disk (after a raw edit) and re-seed all drafts so no
    // page keeps a stale value that a later Save would write back.
    void ReloadSettingsFromDisk();

    // Draft value for a key as a typed QVariant (type resolved from the registered
    // field). Returns an invalid QVariant for unknown keys.
    [[nodiscard]] QVariant FieldValue(const QString& key);

    // Available acceleration-mode tokens for the Playback page, from the host
    // IVideoDecodeCaps probe (off/auto plus GPU-creatable backends). The currently
    // persisted playback.hwdecMode is always included so a stale-but-configured value
    // stays visible rather than snapping to off. Falls back to the full static list
    // when the service is unavailable.
    [[nodiscard]] QStringList AvailableHwdecModes();

    // ── Keybinds page support ────────────────────────────────────────────────
    // All keybind edits are drafts (core in model_, plugin in pluginKeybinds_) and
    // only applied to the live Hotkeys + persisted on Save().
    [[nodiscard]] QVariantList CoreKeybindEntries();
    [[nodiscard]] QVariantList PluginKeybindGroups();
    void BeginCapture(const QString& action, int slot);
    void CaptureKey(int qtKey, int qtMods);
    void CancelCapture();
    void ClearKeybindSlot(const QString& action, int slot);
    [[nodiscard]] bool Capturing() const noexcept;
    [[nodiscard]] QString CapturingActionName() const;
    [[nodiscard]] int CapturingSlot() const noexcept;
    [[nodiscard]] QString KeybindConflict() const;

protected:
    const char* ModuleName() const override;
    std::vector<framelift::Keybind> Keybinds() override;
    void OnInstall(IModuleContext& ctx) override;

Q_SIGNALS:
    void qmlChanged();

private:
    struct FieldMeta
    {
        std::string key;
        int type = 0;
        std::string desc;
        std::string defaultValue;
        bool moduleOwned = false;
        const char* (*getValue)(void*) = nullptr;
        void (*setValue)(void*, const char*) = nullptr;
        void* ud = nullptr;
    };

    [[nodiscard]] ISettingsStore* SettingsStore() const;
    [[nodiscard]] ISettingsRegistry* SettingsReg() const;
    [[nodiscard]] ISettingsPageRegistry* SettingsPageReg() const;
    [[nodiscard]] IPluginCatalog* PluginCatalog() const;

    void SeedFromContext();
    // Ask the active page's view model to re-seed its editable draft from live
    // plugin state, so reopening the settings window never shows a stale draft.
    // Best-effort + page-type-agnostic: invokes a Q_INVOKABLE "load" slot if the
    // model defines one, no-op otherwise.
    void ReseedActivePage();
    void SeedHostValue(const FieldMeta& f);
    void SeedModuleValue(const FieldMeta& f);
    void RefreshFields();
    void ResetValue(const FieldMeta& f);
    void Save();
    void Reset();
    void RegisterBuiltInPages();
    // Nav group for a page id: "core" (a built-in config section this module owns),
    // "system" (its own management pages — Plugins, Advanced), or "plugin" (any other
    // plugin's contributed page).
    [[nodiscard]] const char* PageGroup(const std::string& id) const;
    [[nodiscard]] QVariantMap ActivePageRecord() const;
    void SetCapturing(bool v);
    [[nodiscard]] std::string FindKeyOwnerLabel(const std::string& canonicalKey, const char* exceptAction);

    // Keybind drafts: seed plugin entries, resolve the draft string for an action
    // (core in model_, plugin in pluginKeybinds_), and apply a canonical key into the
    // capturing slot (conflict-checked, no live rebind — Save() applies).
    void SeedPluginKeybinds();
    [[nodiscard]] std::string* DraftForAction(const std::string& action);
    void ApplyCanonicalKey(const std::string& keyStr);

    bool open_ = false;
    bool dirty_ = false;
    bool isCapturing_ = false;

    std::string openSettingsKey_ = "Ctrl+Comma";
    std::vector<FieldMeta> fields_;
    EditModel model_;
    std::string qmlActivePage_;
    std::vector<std::unique_ptr<QObject>> pageModels_;

    // Page ids this module registers as built-in config sections (playback, audio, …).
    // Drives the Core/Plugins/System grouping in the left nav: ids here are "core",
    // the settings-menu's own management pages ("plugins", "advanced") are "system",
    // and everything else (other plugins' pages) is "plugin".
    std::unordered_set<std::string> corePageIds_;

    // Editable draft of one plugin-registered keybind. `draft` is seeded from the
    // plugin's live value on open and only written back (via setStr) on Save().
    struct PluginKeybindDraft
    {
        std::string label;
        std::string action;
        std::string group;
        std::string defaultBind;
        const char* (*getStr)(void*) = nullptr;
        void (*setStr)(void*, const char*) = nullptr;
        void* ud = nullptr;
        std::string draft;
    };

    std::vector<PluginKeybindDraft> pluginKeybinds_;

    std::string capturingName_;
    int capturingSlot_ = 0;
    std::string* capturingDraft_ = nullptr; // &model_.Str(coreKey) or &PluginKeybindDraft::draft
    std::string keybindConflict_;

    // ── QML pages cache ────────────────────────────────────────────────────────
    // QmlPages() re-enumerates the registry and rebuilds a QVariantList on every
    // read. Cache it and invalidate whenever qmlChanged fires (the NOTIFY for the
    // `pages` property).
    mutable QVariantList pagesCache_;
    mutable bool pagesCacheDirty_ = true;
};

FRAMELIFT_MODULE_ENTRY(
    SettingsMenu, {
                      .renderOrder = 50,
                  }
)
