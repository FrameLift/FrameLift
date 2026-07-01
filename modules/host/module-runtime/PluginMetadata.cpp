#include "PluginMetadata.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>

namespace
{
std::string ToStd(const QJsonValue& v)
{
    return v.isString() ? v.toString().toStdString() : std::string();
}

std::vector<std::string> ToStringList(const QJsonValue& v)
{
    std::vector<std::string> out;
    if (v.isArray())
    {
        const QJsonArray arr = v.toArray();
        out.reserve(static_cast<std::size_t>(arr.size()));
        for (const QJsonValue& item : arr)
        {
            if (item.isString())
            {
                out.push_back(item.toString().toStdString());
            }
        }
    }
    return out;
}
} // namespace

PluginMetadata ParsePluginMetadata(const QJsonObject& metaData)
{
    PluginMetadata meta;

    if (!metaData.contains("pluginId"))
    {
        return meta; // valid stays false
    }

    meta.abiVersion = metaData.value("abi").toInt(0);
    meta.enabled = !metaData.contains("enabled") || metaData.value("enabled").toBool(true);
    meta.pluginId = ToStd(metaData.value("pluginId"));
    meta.pluginFile = ToStd(metaData.value("pluginFile"));
    meta.name = ToStd(metaData.value("name"));
    meta.publisher = ToStd(metaData.value("publisher"));
    meta.description = ToStd(metaData.value("description"));
    meta.providesFeatures = ToStringList(metaData.value("providesFeatures"));
    meta.requiresPlugins = ToStringList(metaData.value("requiresPlugins"));
    meta.requiresFeatures = ToStringList(metaData.value("requiresFeatures"));
    meta.optionalPlugins = ToStringList(metaData.value("optionalPlugins"));
    meta.optionalFeatures = ToStringList(metaData.value("optionalFeatures"));
    meta.platforms = ToStringList(metaData.value("platforms"));

    const QJsonArray versionArr = metaData.value("version").toArray();
    for (int i = 0; i < 3 && i < versionArr.size(); ++i)
    {
        meta.version[i] = versionArr.at(i).toInt(0);
    }

    meta.valid = true;
    return meta;
}
