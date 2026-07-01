#pragma once
#include "ModuleSettingsImpl.h"
#include "SettingsRegistry.h"
#include <framelift/services/ISettingsPageRegistry.h>
#include <framelift/services/ISettingsRegistry.h>
#include <framelift/services/ISettingsStore.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Settings;
class QObject;

struct KeybindEntryRec
{
    std::string label;
    std::string actionName;
    const char* (*getStr)(void*) = nullptr;
    void (*setStr)(void*, const char*) = nullptr;
    void* ud = nullptr;
    std::string group;       // owning module display name (for per-plugin grouping)
    std::string defaultBind; // factory-default bind list (for "reset to default")
};

struct ModuleSettingRec
{
    std::string key;
    int type = 0;
    std::string desc;
    std::string defaultValue;
    const char* (*getValue)(void*) = nullptr;
    void (*setValue)(void*, const char*) = nullptr;
    void* ud = nullptr;
};

struct SettingsPageRec
{
    std::string id;
    std::string title;
    std::string qmlUrl;
    QObject* viewModel = nullptr;
    int order = 0;
};

struct ChangeCallbackRec
{
    void (*cb)(void*) = nullptr;
    void* ud = nullptr;
    void (*cleanup)(void*) = nullptr;
};

// The host settings facade. Implements the three closely-coupled settings interfaces —
// the typed value store (ISettingsStore), the registration/enumeration surface
// (ISettingsRegistry), and the QML settings-page directory (ISettingsPageRegistry) —
// which all share the field registry and the change-callback list. Owned by ModuleContext
// and registered under each interface id so plugins reach them via ctx.GetService<T>().
class SettingsService final : public ISettingsStore, public ISettingsRegistry, public ISettingsPageRegistry
{
public:
    SettingsService(Settings* settings, std::string settingsPath);

    // Anchored out-of-line so the implicit virtual destructor (and its secondary-base
    // thunks) is emitted in exactly one TU, the same multiple-inheritance + mingw/LTO
    // concern the old combined context had.
    ~SettingsService() override;

    // ── ISettingsStore: typed getters ──
    float GetSettingFloat(const char* key) const noexcept override;
    bool GetSettingBool(const char* key) const noexcept override;
    int GetSettingInt(const char* key) const noexcept override;
    int GetSettingString(const char* key, char* buf, int cap) const noexcept override;

    // ── ISettingsStore: setters / persistence ──
    void CommitSettingFloat(const char* key, float value) noexcept override;
    void CommitSettingBool(const char* key, bool value) noexcept override;
    void CommitSettingInt(const char* key, int value) noexcept override;
    void CommitSettingString(const char* key, const char* value) noexcept override;
    void SaveSettings() noexcept override;

    int GetSettingsFilePath(char* buf, int cap) const noexcept override;
    void ReloadSettings() noexcept override;

    void RegisterSettingsChangeCallback(void (*cb)(void*), void* ud, void (*cleanup)(void*)) noexcept override;

    IModuleSettings& GetModuleSettings(const char* sectionName) noexcept override;

    // ── ISettingsRegistry ──
    void EnumerateSettings(void (*visit)(const FrameLiftSettingDesc*, void*), void* visitUd) const noexcept override;

    void RegisterModuleSetting(const FrameLiftModuleSettingDesc* desc) noexcept override;
    void EnumerateModuleSettings(
        void (*visit)(const FrameLiftModuleSettingDesc*, void*), void* visitUd
    ) const noexcept override;

    void RegisterKeybindEntry(
        const char* label, const char* actionName, const char* (*getStr)(void*), void (*setStr)(void*, const char*),
        void* ud, const char* group, const char* defaultBind
    ) noexcept override;
    void EnumerateKeybindEntries(
        void (*visit)(
            const char*, const char*, const char* (*)(void*), void (*)(void*, const char*), void*, const char*,
            const char*, void*
        ),
        void* visitUd
    ) const noexcept override;

    // ── ISettingsPageRegistry ──
    void RegisterSettingsPage(
        const char* id, const char* title, const char* qmlUrl, QObject* viewModel, int order
    ) noexcept override;
    void EnumerateSettingsPages(
        void (*visit)(const FrameLiftSettingsPageDesc*, void*), void* visitUd
    ) const noexcept override;

    // Drop all DLL-owned change callbacks (firing their cleanup) and the registration
    // tables before the plugin libraries are unloaded. Called from
    // ModuleContext::ClearSubscriptions().
    void ClearRegistrations();

private:
    std::string settingsPath_;
    Settings* settings_;

    // Field registry bound to *settings_, plus the serialized defaults (for
    // EnumerateSettings' defaultValue). Built once in the constructor.
    SettingsRegistry settingsRegistry_;
    std::unordered_map<std::string, std::string> settingDefaults_;
    std::vector<ChangeCallbackRec> changeCallbacks_;
    std::unordered_map<std::string, std::unique_ptr<ModuleSettingsImpl>> moduleSettings_;
    std::vector<KeybindEntryRec> keybindEntries_;
    std::vector<ModuleSettingRec> moduleSettingEntries_;
    std::vector<SettingsPageRec> settingsPages_;
};
