#pragma once
#include <string>
#include <vector>

class QJsonObject;

// Host-side, owned representation of a plugin's embedded Q_PLUGIN_METADATA JSON.
// Parsed from QPluginLoader::metaData()["MetaData"] — no plugin code is run to read
// it. Metadata now travels as JSON, so the host owns std::string/std::vector
// copies instead of pointers into a loaded DLL.

struct PluginMetadata
{
    bool valid = false; // false if the JSON was missing/unparseable
    int abiVersion = 0;
    bool enabled = true;
    std::string pluginId;
    std::string pluginFile;
    std::string name;
    int version[3] = {0, 0, 0};
    std::string publisher;
    std::string description;
    std::vector<std::string> providesFeatures;
    std::vector<std::string> requiresPlugins;
    std::vector<std::string> requiresFeatures;
    std::vector<std::string> optionalPlugins;
    std::vector<std::string> optionalFeatures;
    std::vector<std::string> platforms; // empty means every platform
};

// Parse the "MetaData" object from a plugin's QPluginLoader::metaData(). On a missing
// or malformed object the result has valid=false (and should be skipped by the loader).
PluginMetadata ParsePluginMetadata(const QJsonObject& metaData);
