#pragma once
#include "FontScan.h"
#include "ModuleSettingsImpl.h"
#include "SettingsRegistry.h"
#include <framelift/IModuleContext.h>
#include <framelift/services/IAppPaths.h>
#include <framelift/services/IFontCatalog.h>
#include <framelift/services/IPackageCatalog.h>
#include <framelift/services/ISettingsRegistry.h>
#include <framelift/services/ISettingsStore.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Settings;
class PackageConfig;
class UIContext;

// Host-side settings page and keybind entry (STL types OK — host-internal only).
struct SettingsPageEntry
{
    std::string title;
    void (*renderFn)(void*, UIContext&) = nullptr;
    void (*applyFn)(void*) = nullptr;
    void* ud = nullptr;
    bool visible = true;
    void (*cleanup)(void*) = nullptr;
};

struct KeybindEntryRec
{
    std::string label;
    std::string actionName;
    const char* (*getStr)(void*) = nullptr;
    void (*setStr)(void*, const char*) = nullptr;
    void* ud = nullptr;
};

struct SubscriptionRec
{
    std::string eventId;
    void (*cb)(const void*, void*) = nullptr;
    void* ud = nullptr;
    void (*cleanup)(void*) = nullptr;
};

struct ChangeCallbackRec
{
    void (*cb)(void*) = nullptr;
    void* ud = nullptr;
    void (*cleanup)(void*) = nullptr;
};

// Concrete host context. Implements the bootstrap IModuleContext plus every host
// capability service, and registers itself under each service id at construction so
// plugins can ctx.GetService<ISettingsStore>() etc. Host code holding a ModuleContext
// directly can still call any method without going through the registry.
class ModuleContext final : public IModuleContext,
                            public ISettingsStore,
                            public ISettingsRegistry,
                            public IPackageCatalog,
                            public IFontCatalog,
                            public IAppPaths
{
public:
    ModuleContext(
        std::string prefPath, Settings* settings, const std::string& settingsPath,
        PackageConfig* packageConfig = nullptr, std::string packagesPath = {}
    );

    int GetPrefPath(char* buf, int cap) const noexcept override;

    void EnumeratePackages(
        void (*visit)(const char*, const FrameLiftPackageInfo&, bool, bool, bool, void*), void* visitUd
    ) const noexcept override;

    void SetPackageEnabled(const char* name, bool enabled) noexcept override;

    void EnumerateSystemFonts(void (*visit)(const char*, const char*, void*), void* visitUd) const noexcept override;

    // Host feeds the package catalogue here after PackageLoader::LoadAll, so
    // SettingsMenu (and any consumer) can list and toggle packages via the ABI.
    // info is the loaded descriptor, or nullptr for a present-but-disabled DLL.
    void AddPackage(std::string name, bool enabled, const FrameLiftPackageInfo* info);

    // Settings getters
    float GetSettingFloat(const char* key) const noexcept override;
    bool GetSettingBool(const char* key) const noexcept override;
    int GetSettingInt(const char* key) const noexcept override;
    int GetSettingString(const char* key, char* buf, int cap) const noexcept override;

    // Settings setters (SettingsMenu only)
    void CommitSettingFloat(const char* key, float value) noexcept override;
    void CommitSettingBool(const char* key, bool value) noexcept override;
    void CommitSettingInt(const char* key, int value) noexcept override;
    void CommitSettingString(const char* key, const char* value) noexcept override;
    void SaveSettings() noexcept override;

    int GetSettingsFilePath(char* buf, int cap) const noexcept override;
    void ReloadSettings() noexcept override;

    void EnumerateSettings(
        void (*visit)(const FrameLiftSettingDesc*, void*), void* visitUd
    ) const noexcept override;

    void RegisterSettingsChangeCallback(void (*cb)(void*), void* ud, void (*cleanup)(void*)) noexcept override;

    IModuleSettings& GetModuleSettings(const char* sectionName) noexcept override;

    void RegisterSettingsPage(
        const char* title, void (*renderFn)(void*, UIContext&), void (*applyFn)(void*), void* ud, bool visible,
        void (*cleanup)(void*)
    ) noexcept override;

    void EnumerateSettingsPages(
        void (*visit)(const char*, void (*)(void*, UIContext&), void (*)(void*), void*, bool, void*), void* visitUd
    ) const noexcept override;

    void RegisterKeybindEntry(
        const char* label, const char* actionName, const char* (*getStr)(void*), void (*setStr)(void*, const char*),
        void* ud
    ) noexcept override;

    void EnumerateKeybindEntries(
        void (*visit)(const char*, const char*, const char* (*)(void*), void (*)(void*, const char*), void*, void*),
        void* visitUd
    ) const noexcept override;

    // Drop all subscriber/DLL-owned callbacks before FreeLibrary.
    void ClearSubscriptions();

    // Host-internal direct access to the live settings (avoids the string-key API).
    [[nodiscard]] Settings& GetSettingsDirect() const
    {
        return *settings_;
    }

protected:
    void* GetServiceRaw(const char* id) const noexcept override;
    void RegisterServiceRaw(const char* id, void* ptr) noexcept override;
    void SubscribeRaw(
        const char* id, void (*cb)(const void*, void*), void* ud, void (*cleanup)(void*)
    ) noexcept override;
    void PublishRaw(const char* id, const void* payload) noexcept override;

private:
    std::string prefPath_;
    std::string settingsPath_;
    Settings* settings_;

    // User package enablement manifest (packages.ini) and its path. Null in contexts
    // that don't manage package enablement (e.g. unit tests).
    PackageConfig* packageConfig_ = nullptr;
    std::string packagesPath_;

    // Field registry bound to *settings_, plus the serialized defaults (for
    // EnumerateSettings' defaultValue). Built once in the constructor.
    SettingsRegistry settingsRegistry_;
    std::unordered_map<std::string, std::string> settingDefaults_;
    std::unordered_map<std::string, void*> registry_;
    std::vector<ChangeCallbackRec> changeCallbacks_;
    std::vector<SubscriptionRec> subscriptions_;
    std::unordered_map<std::string, std::unique_ptr<ModuleSettingsImpl>> moduleSettings_;
    std::vector<SettingsPageEntry> settingsPages_;
    std::vector<KeybindEntryRec> keybindEntries_;

    // One catalogue entry per available package (loaded or merely present).
    struct PackageRec
    {
        std::string name;           // package id / load key — owns stable storage
        bool enabled;               // true once loaded (enablement is JSON/build-time driven)
        bool loadFailed;            // enabled at startup yet not loaded (fixed at build time)
        const FrameLiftPackageInfo* info; // loaded descriptor, or nullptr if not loaded
    };

    std::vector<PackageRec> packageCatalog_;

    // System font catalogue — scanned lazily on first EnumerateSystemFonts call.
    mutable std::vector<FontEntry> fontCache_;
    mutable bool fontsScanned_ = false;
};
