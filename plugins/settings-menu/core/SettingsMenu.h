#pragma once

#include <framelift/core.h>
#include <framelift/services.h>

#include <QtCore/QObject>
#include <QtCore/QVariant>
#include <QtCore/QVariantList>
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

class SettingsMenu final : public QObject, public ModuleBase
{
    Q_OBJECT
    Q_PROPERTY(bool open READ IsOpen NOTIFY qmlChanged)
    Q_PROPERTY(bool dirty READ Dirty NOTIFY qmlChanged)
    Q_PROPERTY(QStringList pages READ QmlPages NOTIFY qmlChanged)
    Q_PROPERTY(QString activePage READ ActivePage WRITE SetActivePage NOTIFY qmlChanged)
    Q_PROPERTY(QVariantList activeFields READ QmlFields NOTIFY qmlChanged)

public:
    bool HandleKeyDownEvent(const AppEvent& e) override;

    void Open() noexcept;
    void Close() noexcept;

    [[nodiscard]] bool IsOpen() const noexcept;
    [[nodiscard]] bool Dirty() const noexcept;
    [[nodiscard]] QStringList QmlPages();
    [[nodiscard]] QString ActivePage() const;
    void SetActivePage(const QString& page);
    [[nodiscard]] QVariantList QmlFields();

    Q_INVOKABLE void setFieldValue(const QString& key, const QVariant& value);
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
        std::string defaultValue;
        bool moduleOwned = false;
        const char* (*getValue)(void*) = nullptr;
        void (*setValue)(void*, const char*) = nullptr;
        void* ud = nullptr;
    };

    [[nodiscard]] ISettingsStore* SettingsStore() const;
    [[nodiscard]] ISettingsRegistry* SettingsReg() const;

    void SeedFromContext();
    void SeedHostValue(const FieldMeta& f);
    void SeedModuleValue(const FieldMeta& f);
    void RefreshFields();
    void ResetValue(const FieldMeta& f);
    void Save();
    void Reset();
    void SetCapturing(bool v);
    [[nodiscard]] std::string FindKeyOwnerLabel(const std::string& canonicalKey, const char* exceptAction);

    bool open_ = false;
    bool dirty_ = false;
    bool isCapturing_ = false;

    std::string openSettingsKey_ = "Ctrl+Comma";
    std::vector<FieldMeta> fields_;
    EditModel model_;
    std::string qmlActivePage_ = "general";

    std::string capturingName_;
    int capturingSlot_ = 0;
    std::string* capturingBind_ = nullptr;
    const char* (*capturingGetStr_)(void*) = nullptr;
    void (*capturingSetStr_)(void*, const char*) = nullptr;
    void* capturingUd_ = nullptr;
    std::string keybindConflict_;
};

FRAMELIFT_MODULE_ENTRY(
    SettingsMenu, {
                      .renderOrder = 50,
                  }
)
