#pragma once
#include "PluginMetadata.h"
#include <framelift/IModule.h>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

class IPlugin;
class QPluginLoader;
class QObject;

// Loads every plugin DLL/SO present in a directory. A plugin's root object is a
// QObject implementing IPlugin (see sdk/include/framelift/IPlugin.h), carrying
// Q_PLUGIN_METADATA whose JSON gives the plugin identity + ABI. The loader reads
// that metadata via QPluginLoader::metaData() — without instantiating — to ABI-check
// and resolve dependencies, then instantiates the plugin's single module.
class PluginLoader
{
public:
    struct LoadedPlugin
    {
        std::string pluginId;                  // plugin id / enabled-list entry
        std::string pluginFile;                // shipped plugin binary filename
        std::unique_ptr<QPluginLoader> loader; // owns the plugin's root IPlugin instance
        IPlugin* plugin;                       // qobject_cast<IPlugin*>(loader->instance())
        PluginMetadata meta;                   // owned copy of the embedded metadata
        IModule* module = nullptr;
        QObject* viewModel = nullptr; // may be nullptr
        std::string qmlEntryUrl;
        int renderOrder = 0;
    };

    struct AvailablePlugin
    {
        std::string pluginId;
        std::string pluginFile;
        std::string displayName; // metadata name, or pluginFile when metadata is unreadable
        int version[3] = {0, 0, 0};
        std::string publisher;
        std::string description;
    };

    PluginLoader();
    // Destroys every instantiated module via IPlugin::DestroyModule and unloads each plugin.
    ~PluginLoader();

    // Scans plugins/ for shared libraries and loads every ABI-compatible plugin
    // whose id is NOT in `disabledPlugins`. Dependencies and load order are resolved
    // from embedded plugin metadata before any module object is created. Does NOT
    // call Install(); the caller does that via Registry().Add(module, ctx).
    void LoadAll(const std::string& pluginsDir, const std::unordered_set<std::string>& disabledPlugins = {});

    const std::vector<LoadedPlugin>& Plugins() const
    {
        return plugins_;
    }

    // Metadata captured by the same scan LoadAll() used for resolution/loading.
    const std::vector<AvailablePlugin>& AvailablePlugins() const
    {
        return availablePlugins_;
    }

private:
    std::vector<LoadedPlugin> plugins_;
    std::vector<AvailablePlugin> availablePlugins_;
};
