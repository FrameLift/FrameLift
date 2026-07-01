#include "PluginCatalog.h"
#include "PluginConfig.h"
#include <utility>

void PluginCatalog::AddPlugin(PluginCatalogEntry entry)
{
    PluginCatalogRec rec;
    rec.id = std::move(entry.id);
    rec.displayName = std::move(entry.displayName);
    rec.version[0] = entry.version[0];
    rec.version[1] = entry.version[1];
    rec.version[2] = entry.version[2];
    rec.publisher = std::move(entry.publisher);
    rec.description = std::move(entry.description);
    rec.enabled = entry.enabled;
    rec.loaded = entry.loaded;
    rec.loadFailed = rec.enabled && !rec.loaded;
    pluginCatalog_.push_back(std::move(rec));
}

void PluginCatalog::EnumeratePlugins(
    void (*visit)(const char*, const char*, const int*, const char*, const char*, bool, bool, bool, void*),
    void* visitUd
) const noexcept
{
    if (!visit)
    {
        return;
    }
    for (const auto& rec : pluginCatalog_)
    {
        visit(
            rec.id.c_str(), rec.displayName.c_str(), rec.version, rec.publisher.c_str(), rec.description.c_str(),
            rec.enabled, rec.loaded, rec.loadFailed, visitUd
        );
    }
}

void PluginCatalog::SetPluginEnabled(const char* pluginId, bool enabled) noexcept
{
    if (!pluginId)
    {
        return;
    }

    bool found = false;
    for (auto& plugin : pluginCatalog_)
    {
        if (plugin.id == pluginId)
        {
            plugin.enabled = enabled; // reflect immediately so the UI checkbox updates
            found = true;
        }
    }
    if (!found)
    {
        return; // unknown module
    }

    // Persist to the opt-out manifest; the change takes effect on the next launch,
    // so there is no live state to notify (skip the change-callback fan-out).
    if (pluginConfig_)
    {
        pluginConfig_->Set(pluginId, enabled);
        pluginConfig_->Save(pluginsPath_);
    }
}
