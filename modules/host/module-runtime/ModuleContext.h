#pragma once
#include "AppPaths.h"
#include "PluginCatalog.h"
#include "SettingsService.h"
#include <framelift/IModuleContext.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Settings;
class PluginConfig;

struct SubscriptionRec
{
    std::string eventId;
    void (*cb)(const void*, void*) = nullptr;
    void* ud = nullptr;
    void (*cleanup)(void*) = nullptr;
};

// Concrete host bootstrap context. Implements ONLY the frozen Tier-1 IModuleContext
// surface (the service registry + the pub/sub bus). Every host capability —
// settings, the keybind/page registries, the plugin catalogue, paths — lives in a
// small, single-purpose sub-service that this context owns and registers under its
// interface id at construction, so plugins discover each with ctx.GetService<T>().
// Host code holding a ModuleContext reaches the sub-services through the accessors.
class ModuleContext final : public IModuleContext
{
public:
    ModuleContext(
        std::string prefPath, Settings* settings, const std::string& settingsPath,
        PluginConfig* pluginConfig = nullptr, std::string pluginsPath = {}
    );

    ~ModuleContext() override;

    // ── Owned capability services (host-internal accessors) ──
    [[nodiscard]] SettingsService& Settings() noexcept
    {
        return settings_;
    }
    [[nodiscard]] PluginCatalog& Catalog() noexcept
    {
        return catalog_;
    }
    [[nodiscard]] AppPaths& Paths() noexcept
    {
        return paths_;
    }

    // Drop all subscriber/DLL-owned callbacks before FreeLibrary: this context's own
    // pub/sub subscriptions, then the settings sub-service's registration tables.
    void ClearSubscriptions();

protected:
    void* GetServiceRaw(const char* id) const noexcept override;
    void RegisterServiceRaw(const char* id, void* ptr) noexcept override;
    void SubscribeRaw(
        const char* id, void (*cb)(const void*, void*), void* ud, void (*cleanup)(void*)
    ) noexcept override;
    void PublishRaw(const char* id, const void* payload) noexcept override;

private:
    // Owned capability services. Declared before the registry so they outlive any
    // service-pointer entries registered into it.
    AppPaths paths_;
    PluginCatalog catalog_;
    SettingsService settings_;

    std::unordered_map<std::string, void*> registry_;
    std::vector<SubscriptionRec> subscriptions_;
};
