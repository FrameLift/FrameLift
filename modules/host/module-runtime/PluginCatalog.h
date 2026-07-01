#pragma once
#include <framelift/services/IPluginCatalog.h>
#include <string>
#include <vector>

class PluginConfig;

// Catalogue of every plugin discovered in plugins/ (loaded or merely present). Owned by
// ModuleContext and registered under IPluginCatalog::InterfaceId so SettingsMenu can list
// and toggle plugins via the ABI. The host calls AddPlugin() directly after PluginLoader
// discovery; plugins consume it through ctx.GetService<IPluginCatalog>().
class PluginCatalog final : public IPluginCatalog
{
public:
    // pluginConfig/pluginsPath are null/empty in contexts that don't manage plugin
    // enablement (e.g. unit tests); SetPluginEnabled then only updates the live row.
    PluginCatalog(PluginConfig* pluginConfig, std::string pluginsPath)
        : pluginConfig_(pluginConfig), pluginsPath_(std::move(pluginsPath))
    {
    }

    // A plugin present in plugins/ (loaded or merely discovered). Built by the host and
    // handed to AddPlugin after PluginLoader::LoadAll.
    struct PluginCatalogEntry
    {
        std::string id;
        std::string displayName;
        int version[3] = {0, 0, 0};
        std::string publisher;
        std::string description;
        bool enabled = true;
        bool loaded = false;
    };

    void AddPlugin(PluginCatalogEntry entry);

    void EnumeratePlugins(
        void (*visit)(const char*, const char*, const int*, const char*, const char*, bool, bool, bool, void*),
        void* visitUd
    ) const noexcept override;

    void SetPluginEnabled(const char* pluginId, bool enabled) noexcept override;

private:
    PluginConfig* pluginConfig_ = nullptr;
    std::string pluginsPath_;

    // One entry per available plugin. loadFailed is computed once in AddPlugin and
    // snapshots startup state, so a freshly toggled plugin reads as "pending restart",
    // not "failed".
    struct PluginCatalogRec
    {
        std::string id;
        std::string displayName;
        int version[3];
        std::string publisher;
        std::string description;
        bool enabled;
        bool loaded;
        bool loadFailed;
    };

    std::vector<PluginCatalogRec> pluginCatalog_;
};
