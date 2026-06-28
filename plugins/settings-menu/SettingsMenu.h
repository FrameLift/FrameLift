#pragma once

#include <framelift/core.h>
#include <framelift/services.h>

#include <QtCore/QObject>
#include <QtCore/QVariant>
#include <QtCore/QVariantList>
#include <memory>
#include <string>
#include <unordered_map>
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

    Q_INVOKABLE void setFieldValue(const QString& key, const QVariant& value);
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
    Q_INVOKABLE void saveQml();
    Q_INVOKABLE void resetAllQml();
    Q_INVOKABLE void closeQml();

    [[nodiscard]] bool SettingBool(const std::string& key);
    [[nodiscard]] int SettingInt(const std::string& key);
    [[nodiscard]] float SettingFloat(const std::string& key);
    [[nodiscard]] std::string SettingString(const std::string& key);

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
    [[nodiscard]] QVariantMap ActivePageRecord() const;
    void SetCapturing(bool v);
    [[nodiscard]] std::string FindKeyOwnerLabel(const std::string& canonicalKey, const char* exceptAction);

    bool open_ = false;
    bool dirty_ = false;
    bool isCapturing_ = false;

    std::string openSettingsKey_ = "Ctrl+Comma";
    std::vector<FieldMeta> fields_;
    EditModel model_;
    std::string qmlActivePage_;
    std::vector<std::unique_ptr<QObject>> pageModels_;

    std::string capturingName_;
    int capturingSlot_ = 0;
    std::string* capturingBind_ = nullptr;
    const char* (*capturingGetStr_)(void*) = nullptr;
    void (*capturingSetStr_)(void*, const char*) = nullptr;
    void* capturingUd_ = nullptr;
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
