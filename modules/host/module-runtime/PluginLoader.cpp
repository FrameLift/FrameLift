#include "PluginLoader.h"
#include "PluginResolver.h"
#include <chrono>
#include <filesystem>
#include <framelift/IPlugin.h>
#include <framelift/Log.h>
#include <framelift/ModuleABI.h>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <QtCore/QJsonObject>
#include <QtCore/QPluginLoader>
#include <QtCore/QString>

Log::SinkFn HostLogSink();

namespace
{
#ifdef _WIN32
constexpr const char* kPluginExt = ".dll";
#else
constexpr const char* kPluginExt = ".so";
#endif

struct PluginBinary
{
    std::string pluginFile;
    std::string path;
};

struct PluginCandidate
{
    PluginBinary binary;
    std::unique_ptr<QPluginLoader> loader;
    PluginMetadata meta;
};

std::vector<PluginBinary> DiscoverPluginBinaries(const std::string& pluginsDir)
{
    namespace fs = std::filesystem;
    std::vector<PluginBinary> out;

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(pluginsDir, ec))
    {
        std::error_code fec;
        if (!entry.is_regular_file(fec) || entry.path().extension() != kPluginExt)
        {
            continue;
        }

        out.push_back({entry.path().filename().string(), entry.path().string()});
    }

    return out;
}

// Read a plugin's embedded Q_PLUGIN_METADATA without loading the plugin:
// metaData() works on an unloaded QPluginLoader, and the author JSON sits under
// "MetaData".
PluginMetadata ReadPluginMetadata(QPluginLoader& loader)
{
    return ParsePluginMetadata(loader.metaData().value("MetaData").toObject());
}

bool AbiCompatible(const PluginMetadata& meta)
{
    return meta.valid && FrameLiftAbiCompatible(meta.abiVersion, FRAMELIFT_ABI_VERSION);
}
} // namespace

PluginLoader::PluginLoader() = default;

void PluginLoader::LoadAll(const std::string& pluginsDir, const std::unordered_set<std::string>& disabledPlugins)
{
    using Clock = std::chrono::steady_clock;
    const auto loadStart = Clock::now();

    // Read metadata for every plugin present in the directory (dedupe by id). No
    // plugin code runs here; metaData() reads the embedded JSON only.
    std::vector<PluginCandidate> candidates;
    std::unordered_map<std::string, std::size_t> seenIds;
    for (const auto& binary : DiscoverPluginBinaries(pluginsDir))
    {
        auto loader = std::make_unique<QPluginLoader>(QString::fromStdString(binary.path));
        PluginMetadata meta = ReadPluginMetadata(*loader);
        if (!meta.valid)
        {
            Log::Warn("Plugin '{}': missing/invalid metadata - rebuild against current SDK", binary.pluginFile);
            continue;
        }
        if (!AbiCompatible(meta))
        {
            Log::Warn(
                "Plugin '{}' v{}.{}.{}: ABI version {} incompatible with host version {} - rebuild against current SDK",
                meta.pluginId, meta.version[0], meta.version[1], meta.version[2], meta.abiVersion,
                FRAMELIFT_ABI_VERSION
            );
            continue;
        }
        if (seenIds.contains(meta.pluginId))
        {
            Log::Warn("Plugin '{}': duplicate plugin id - skipped", meta.pluginId);
            continue;
        }
        seenIds.emplace(meta.pluginId, candidates.size());
        candidates.push_back(PluginCandidate{binary, std::move(loader), std::move(meta)});
    }

    // Resolve dependencies over every discovered plugin, then load the accepted ones
    // in dependency order (providers before consumers).
    std::vector<PluginResolveCandidate> resolveCandidates;
    resolveCandidates.reserve(candidates.size());
    for (const auto& candidate : candidates)
    {
        resolveCandidates.push_back({&candidate.meta});
    }

    const std::vector<PluginResolveDecision> decisions =
        ResolvePlugins(resolveCandidates, FrameLiftCurrentPlatformId());

    std::vector<PluginResolveCandidate> acceptedResolve;
    std::vector<std::size_t> acceptedIndex;
    for (std::size_t i = 0; i < candidates.size(); ++i)
    {
        if (decisions[i].accepted)
        {
            acceptedResolve.push_back({&candidates[i].meta});
            acceptedIndex.push_back(i);
        }
        else
        {
            Log::Warn("Plugin '{}': {} - skipped", candidates[i].meta.pluginId, decisions[i].reason);
        }
    }

    // Diagnose colliding providers: two accepted plugins declaring the same feature.
    // Plugin ids are already deduped above. We keep first-wins but warn so a
    // collision is visible rather than silent.
    {
        std::unordered_map<std::string, std::string> firstProvider; // feature -> plugin id
        for (const auto& accepted : acceptedResolve)
        {
            const PluginMetadata& meta = *accepted.meta;
            for (const std::string& feature : meta.providesFeatures)
            {
                if (feature.empty())
                {
                    continue;
                }
                const auto [it, inserted] = firstProvider.emplace(feature, meta.pluginId);
                if (!inserted)
                {
                    Log::Warn(
                        "Plugin '{}': feature '{}' already provided by '{}' - keeping the first provider",
                        meta.pluginId, feature, it->second
                    );
                }
            }
        }
    }

    for (const std::size_t orderIdx : OrderPlugins(acceptedResolve))
    {
        PluginCandidate& candidate = candidates[acceptedIndex[orderIdx]];
        const PluginMetadata& meta = candidate.meta;

        if (disabledPlugins.contains(meta.pluginId))
        {
            Log::Info("Plugin '{}': disabled by user - skipped", meta.pluginId);
            continue;
        }

        const auto pluginStart = Clock::now();

        QObject* const root = candidate.loader->instance();
        if (!root)
        {
            Log::Warn(
                "Plugin '{}': instance() failed ({}) - skipped", meta.pluginId,
                candidate.loader->errorString().toStdString()
            );
            continue;
        }
        IPlugin* const plugin = qobject_cast<IPlugin*>(root);
        if (!plugin)
        {
            Log::Warn("Plugin '{}': root object is not an IPlugin - skipped", meta.pluginId);
            candidate.loader->unload();
            continue;
        }

        plugin->SetLogSink(HostLogSink());

        IModule* const instance = plugin->CreateModule();
        if (!instance)
        {
            Log::Warn("Plugin '{}': CreateModule() returned nullptr - skipped", meta.pluginId);
            candidate.loader->unload();
            continue;
        }

        const int order = plugin->RenderOrder();
        QObject* const viewModel = plugin->GetViewModel(instance);
        const char* const qmlEntry = plugin->QmlEntryUrl();
        plugins_.push_back(
            {meta.pluginId, candidate.binary.pluginFile, std::move(candidate.loader), plugin, candidate.meta, instance,
             viewModel, qmlEntry ? std::string(qmlEntry) : std::string{}, order}
        );

        const std::string by = meta.publisher.empty() ? std::string() : std::string(" by ") + meta.publisher;
        const double pluginMs = std::chrono::duration<double, std::milli>(Clock::now() - pluginStart).count();
        Log::Info(
            "Plugin '{}' v{}.{}.{}{} loaded (abi version {}, {:.1f} ms)", meta.pluginId, meta.version[0],
            meta.version[1], meta.version[2], by, meta.abiVersion, pluginMs
        );
    }

    const double totalMs = std::chrono::duration<double, std::milli>(Clock::now() - loadStart).count();
    Log::Info("Loaded {} plugin(s) in {:.1f} ms", plugins_.size(), totalMs);
}

std::vector<PluginLoader::AvailablePlugin> PluginLoader::DiscoverAvailable(const std::string& pluginsDir)
{
    std::vector<AvailablePlugin> out;
    for (const auto& binary : DiscoverPluginBinaries(pluginsDir))
    {
        QPluginLoader loader(QString::fromStdString(binary.path));
        const PluginMetadata meta = ReadPluginMetadata(loader);
        if (meta.valid && AbiCompatible(meta) && !meta.pluginId.empty())
        {
            AvailablePlugin plugin;
            plugin.pluginId = meta.pluginId;
            plugin.pluginFile = binary.pluginFile;
            plugin.displayName = meta.name.empty() ? meta.pluginId : meta.name;
            plugin.version[0] = meta.version[0];
            plugin.version[1] = meta.version[1];
            plugin.version[2] = meta.version[2];
            plugin.publisher = meta.publisher;
            plugin.description = meta.description;
            out.push_back(std::move(plugin));
        }
        else
        {
            out.push_back({binary.pluginFile, binary.pluginFile, binary.pluginFile});
        }
    }
    return out;
}

PluginLoader::~PluginLoader()
{
    for (auto& p : plugins_)
    {
        if (p.module)
        {
            p.plugin->DestroyModule(p.module);
        }
        if (p.loader)
        {
            p.loader->unload();
        }
    }
}
