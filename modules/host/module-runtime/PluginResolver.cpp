#include "PluginResolver.h"
#include <algorithm>
#include <cstddef>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace
{
bool Contains(const std::vector<std::string>& list, std::string_view value)
{
    return std::ranges::find(list, value) != list.end();
}

void AddAll(std::unordered_set<std::string>& out, const std::vector<std::string>& list)
{
    for (const std::string& item : list)
    {
        if (!item.empty())
        {
            out.emplace(item);
        }
    }
}

bool PlatformSupported(const PluginMetadata& meta, std::string_view platformId)
{
    return meta.platforms.empty() || Contains(meta.platforms, platformId);
}

std::string PluginId(const PluginMetadata* meta)
{
    return meta && !meta->pluginId.empty() ? meta->pluginId : "<unknown>";
}
} // namespace

const char* FrameLiftCurrentPlatformId() noexcept
{
#ifdef _WIN32
    return "windows";
#elif defined(__linux__)
    return "linux";
#elif defined(__APPLE__)
    return "macos";
#else
    return "unknown";
#endif
}

std::vector<PluginResolveDecision> ResolvePlugins(
    const std::vector<PluginResolveCandidate>& candidates, std::string_view platformId,
    std::span<const std::string_view> hostFeatures
)
{
    std::vector<PluginResolveDecision> decisions(candidates.size());
    std::unordered_set<std::string> hostProvidedFeatures;
    for (const std::string_view feature : hostFeatures)
    {
        if (!feature.empty())
        {
            hostProvidedFeatures.emplace(feature);
        }
    }

    for (std::size_t i = 0; i < candidates.size(); ++i)
    {
        const PluginMetadata* meta = candidates[i].meta;
        if (!meta || !meta->valid)
        {
            decisions[i].reason = "missing plugin metadata";
            continue;
        }
        if (meta->pluginId.empty())
        {
            decisions[i].reason = "missing plugin id";
            continue;
        }
        if (!meta->enabled)
        {
            decisions[i].reason = "plugin disabled by metadata";
            continue;
        }
        if (!candidates[i].enabledByUser)
        {
            decisions[i].reason = "plugin disabled by user";
            continue;
        }

        decisions[i].accepted = true;
        if (!PlatformSupported(*meta, platformId))
        {
            decisions[i].accepted = false;
            decisions[i].reason = "plugin does not support platform '" + std::string(platformId) + "'";
        }
    }

    // Claim plugin-provided features in ascending plugin-id order. Built-in host
    // capabilities always win; among plugins the lowest id wins deterministically.
    // Collision losers stay rejected even if the winner later fails a dependency.
    std::vector<std::size_t> claimOrder;
    claimOrder.reserve(candidates.size());
    for (std::size_t i = 0; i < candidates.size(); ++i)
    {
        if (decisions[i].accepted)
        {
            claimOrder.push_back(i);
        }
    }
    std::ranges::sort(
        claimOrder,
        [&](const std::size_t a, const std::size_t b)
        {
            return candidates[a].meta->pluginId < candidates[b].meta->pluginId;
        }
    );

    std::unordered_map<std::string, std::string> pluginFeatureProviders;
    for (const std::size_t i : claimOrder)
    {
        const PluginMetadata* meta = candidates[i].meta;
        for (const std::string& feature : meta->providesFeatures)
        {
            if (feature.empty())
            {
                continue;
            }
            if (hostProvidedFeatures.contains(feature))
            {
                decisions[i].accepted = false;
                decisions[i].reason = PluginId(meta) + " conflicts with host-provided feature '" + feature + "'";
                break;
            }
            const auto provider = pluginFeatureProviders.find(feature);
            if (provider != pluginFeatureProviders.end() && provider->second != meta->pluginId)
            {
                decisions[i].accepted = false;
                decisions[i].reason =
                    PluginId(meta) + " conflicts with feature '" + feature + "' provided by '" + provider->second + "'";
                break;
            }
        }
        if (decisions[i].accepted)
        {
            for (const std::string& feature : meta->providesFeatures)
            {
                if (!feature.empty())
                {
                    pluginFeatureProviders.try_emplace(feature, meta->pluginId);
                }
            }
        }
    }

    bool changed = true;
    while (changed)
    {
        changed = false;

        std::unordered_set<std::string> providedPlugins;
        std::unordered_set<std::string> providedFeatures = hostProvidedFeatures;
        for (std::size_t i = 0; i < candidates.size(); ++i)
        {
            if (!decisions[i].accepted)
            {
                continue;
            }
            providedPlugins.emplace(candidates[i].meta->pluginId);
            AddAll(providedFeatures, candidates[i].meta->providesFeatures);
        }

        for (std::size_t i = 0; i < candidates.size(); ++i)
        {
            if (!decisions[i].accepted)
            {
                continue;
            }

            const PluginMetadata* meta = candidates[i].meta;
            for (const std::string& required : meta->requiresPlugins)
            {
                if (!required.empty() && !providedPlugins.contains(required))
                {
                    decisions[i].accepted = false;
                    decisions[i].reason = PluginId(meta) + " requires missing plugin '" + required + "'";
                    changed = true;
                    break;
                }
            }
            for (const std::string& required : meta->requiresFeatures)
            {
                if (!decisions[i].accepted)
                {
                    break;
                }
                if (!required.empty() && !providedFeatures.contains(required))
                {
                    decisions[i].accepted = false;
                    decisions[i].reason = PluginId(meta) + " requires missing feature '" + required + "'";
                    changed = true;
                    break;
                }
            }
        }
    }

    return decisions;
}

std::vector<std::size_t> OrderPlugins(const std::vector<PluginResolveCandidate>& candidates)
{
    const std::size_t n = candidates.size();

    // Per plugin: the set of tokens it provides (plugin id + features) and the set
    // it depends on (requires + optional, plugins + features).
    std::vector<std::unordered_set<std::string>> provides(n);
    std::vector<std::unordered_set<std::string>> needs(n);
    std::vector<std::string> ids(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        const PluginMetadata* meta = candidates[i].meta;
        ids[i] = PluginId(meta);
        if (!meta)
        {
            continue;
        }
        provides[i].emplace(meta->pluginId);
        AddAll(provides[i], meta->providesFeatures);
        AddAll(needs[i], meta->requiresPlugins);
        AddAll(needs[i], meta->requiresFeatures);
        AddAll(needs[i], meta->optionalPlugins);
        AddAll(needs[i], meta->optionalFeatures);
    }

    // Edge i → j when consumer j depends on a token provider i offers. Dedupe edges
    // so indegree counts distinct predecessors.
    std::vector<std::set<std::size_t>> succ(n);
    std::vector<int> indeg(n, 0);
    for (std::size_t j = 0; j < n; ++j)
    {
        for (std::size_t i = 0; i < n; ++i)
        {
            if (i == j)
            {
                continue;
            }
            bool dependsOnI = false;
            for (const auto& token : needs[j])
            {
                if (provides[i].contains(token))
                {
                    dependsOnI = true;
                    break;
                }
            }
            if (dependsOnI && succ[i].insert(j).second)
            {
                ++indeg[j];
            }
        }
    }

    // Kahn's algorithm, draining zero-indegree nodes in ascending plugin-id order
    // for determinism. Any nodes left in a cycle are appended by plugin-id order.
    std::vector<std::size_t> order;
    order.reserve(n);
    std::vector<bool> done(n, false);
    while (order.size() < n)
    {
        std::size_t pick = n;
        for (std::size_t i = 0; i < n; ++i)
        {
            if (done[i] || indeg[i] != 0)
            {
                continue;
            }
            if (pick == n || ids[i] < ids[pick])
            {
                pick = i;
            }
        }
        if (pick == n)
        {
            // Cycle: append the remaining nodes in plugin-id order and stop.
            std::vector<std::size_t> rest;
            for (std::size_t i = 0; i < n; ++i)
            {
                if (!done[i])
                {
                    rest.push_back(i);
                }
            }
            std::ranges::sort(
                rest,
                [&](std::size_t a, std::size_t b)
                {
                    return ids[a] < ids[b];
                }
            );
            for (const std::size_t i : rest)
            {
                order.push_back(i);
            }
            break;
        }

        done[pick] = true;
        order.push_back(pick);
        for (const std::size_t s : succ[pick])
        {
            --indeg[s];
        }
    }

    return order;
}
