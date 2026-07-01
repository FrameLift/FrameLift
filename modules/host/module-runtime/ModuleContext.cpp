#include "ModuleContext.h"
#include <cstddef>
#include <utility>

// ::Settings is qualified to the global type: inside this out-of-line definition the
// class name lookup would otherwise resolve "Settings" to the Settings() accessor.
ModuleContext::ModuleContext(
    std::string prefPath, ::Settings* settings, const std::string& settingsPath, PluginConfig* pluginConfig,
    std::string pluginsPath
)
    : paths_(std::move(prefPath)), catalog_(pluginConfig, std::move(pluginsPath)),
      settings_(settings, settingsPath)
{
    // Register every host capability service so plugins reach it via ctx.GetService<T>().
    // The IModuleContext vtable stays at the four bootstrap methods; these are the
    // discoverable Tier-2 services, each implemented by an owned sub-service object.
    RegisterServiceRaw(IAppPaths::InterfaceId, static_cast<IAppPaths*>(&paths_));
    RegisterServiceRaw(IPluginCatalog::InterfaceId, static_cast<IPluginCatalog*>(&catalog_));
    RegisterServiceRaw(ISettingsStore::InterfaceId, static_cast<ISettingsStore*>(&settings_));
    RegisterServiceRaw(ISettingsRegistry::InterfaceId, static_cast<ISettingsRegistry*>(&settings_));
    RegisterServiceRaw(ISettingsPageRegistry::InterfaceId, static_cast<ISettingsPageRegistry*>(&settings_));
    RegisterServiceRaw(ICommandRegistry::InterfaceId, static_cast<ICommandRegistry*>(&commands_));
}

ModuleContext::~ModuleContext() = default;

// ── Service registry ──────────────────────────────────────────────────────────

void* ModuleContext::GetServiceRaw(const char* id) const noexcept
{
    const auto it = registry_.find(id);
    return it != registry_.end() ? it->second : nullptr;
}

void ModuleContext::RegisterServiceRaw(const char* id, void* ptr) noexcept
{
    registry_[id] = ptr;
}

// ── Pub/sub ───────────────────────────────────────────────────────────────────

void ModuleContext::SubscribeRaw(
    const char* id, void (*cb)(const void*, void*), void* ud, void (*cleanup)(void*)
) noexcept
{
    subscriptions_.push_back({id, cb, ud, cleanup});
}

void ModuleContext::PublishRaw(const char* id, const void* payload) noexcept
{
    // Reentrancy-safe: a callback may SubscribeRaw() (the vector may reallocate)
    // or even ClearSubscriptions(). Index + per-step bound check instead of
    // iterators; subscribers added during dispatch do not see the in-flight
    // event (count snapshot), and cb/ud are copied out before the call so a
    // reallocation inside the callback cannot invalidate them.
    const std::size_t count = subscriptions_.size();
    for (std::size_t i = 0; i < count && i < subscriptions_.size(); ++i)
    {
        if (subscriptions_[i].eventId == id)
        {
            const auto cb = subscriptions_[i].cb;
            void* const ud = subscriptions_[i].ud;
            cb(payload, ud);
        }
    }
}

// ── Cleanup ───────────────────────────────────────────────────────────────────

void ModuleContext::ClearSubscriptions()
{
    for (const auto& s : subscriptions_)
    {
        if (s.cleanup)
        {
            s.cleanup(s.ud);
        }
    }
    subscriptions_.clear();

    settings_.ClearRegistrations();
    commands_.ClearPluginCommands();
}
