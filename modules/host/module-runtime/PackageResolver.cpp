#include "PackageResolver.h"
#include <algorithm>
#include <cstddef>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>

namespace
{
bool Contains(const FrameLiftStringList& list, std::string_view value)
{
    for (int i = 0; i < list.count; ++i)
    {
        const char* item = list.items ? list.items[i] : nullptr;
        if (item && std::string_view(item) == value)
        {
            return true;
        }
    }
    return false;
}

void AddAll(std::unordered_set<std::string>& out, const FrameLiftStringList& list)
{
    for (int i = 0; i < list.count; ++i)
    {
        const char* item = list.items ? list.items[i] : nullptr;
        if (item && item[0])
        {
            out.emplace(item);
        }
    }
}

bool PlatformSupported(const FrameLiftModuleInfo& module, std::string_view platformId)
{
    return module.platforms.count <= 0 || Contains(module.platforms, platformId);
}

std::string PackageId(const FrameLiftPackageInfo* info)
{
    return info && info->packageId ? info->packageId : "<unknown>";
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

std::vector<PackageResolveDecision> ResolvePackages(
    const std::vector<PackageResolveCandidate>& candidates, std::string_view platformId
)
{
    std::vector<PackageResolveDecision> decisions(candidates.size());

    for (std::size_t i = 0; i < candidates.size(); ++i)
    {
        const FrameLiftPackageInfo* info = candidates[i].info;
        if (!info)
        {
            decisions[i].reason = "missing package metadata";
            continue;
        }
        if (!info->packageId || !info->packageId[0])
        {
            decisions[i].reason = "missing package id";
            continue;
        }
        if (!info->modules || info->moduleCount <= 0)
        {
            decisions[i].reason = "package has no enabled modules";
            continue;
        }

        decisions[i].accepted = true;
        for (int m = 0; m < info->moduleCount; ++m)
        {
            const FrameLiftModuleInfo& module = info->modules[m];
            if (!module.id || !module.id[0])
            {
                decisions[i].accepted = false;
                decisions[i].reason = "module is missing id";
                break;
            }
            if (!PlatformSupported(module, platformId))
            {
                decisions[i].accepted = false;
                decisions[i].reason = "module '" + std::string(module.id) + "' does not support platform '" +
                                      std::string(platformId) + "'";
                break;
            }
        }
    }

    bool changed = true;
    while (changed)
    {
        changed = false;

        std::unordered_set<std::string> providedModules;
        std::unordered_set<std::string> providedFeatures;
        for (std::size_t i = 0; i < candidates.size(); ++i)
        {
            if (!decisions[i].accepted)
            {
                continue;
            }
            const FrameLiftPackageInfo* info = candidates[i].info;
            for (int m = 0; m < info->moduleCount; ++m)
            {
                const FrameLiftModuleInfo& module = info->modules[m];
                providedModules.emplace(module.id);
                AddAll(providedFeatures, module.providesFeatures);
            }
        }

        for (std::size_t i = 0; i < candidates.size(); ++i)
        {
            if (!decisions[i].accepted)
            {
                continue;
            }

            const FrameLiftPackageInfo* info = candidates[i].info;
            for (int m = 0; m < info->moduleCount && decisions[i].accepted; ++m)
            {
                const FrameLiftModuleInfo& module = info->modules[m];
                for (int r = 0; r < module.requiresModules.count; ++r)
                {
                    const char* required = module.requiresModules.items ? module.requiresModules.items[r] : nullptr;
                    if (required && !providedModules.contains(required))
                    {
                        decisions[i].accepted = false;
                        decisions[i].reason = PackageId(info) + " requires missing module '" + required + "'";
                        changed = true;
                        break;
                    }
                }
                for (int r = 0; r < module.requiresFeatures.count && decisions[i].accepted; ++r)
                {
                    const char* required = module.requiresFeatures.items ? module.requiresFeatures.items[r] : nullptr;
                    if (required && !providedFeatures.contains(required))
                    {
                        decisions[i].accepted = false;
                        decisions[i].reason = PackageId(info) + " requires missing feature '" + required + "'";
                        changed = true;
                        break;
                    }
                }
            }
        }
    }

    return decisions;
}

std::vector<std::size_t> OrderPackages(const std::vector<PackageResolveCandidate>& candidates)
{
    const std::size_t n = candidates.size();

    // Per package: the set of tokens it provides (module ids + features) and the set
    // it depends on (requires + optional, modules + features).
    std::vector<std::unordered_set<std::string>> provides(n);
    std::vector<std::unordered_set<std::string>> needs(n);
    std::vector<std::string> ids(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        const FrameLiftPackageInfo* info = candidates[i].info;
        ids[i] = PackageId(info);
        if (!info)
        {
            continue;
        }
        for (int m = 0; m < info->moduleCount; ++m)
        {
            const FrameLiftModuleInfo& module = info->modules[m];
            if (module.id && module.id[0])
            {
                provides[i].emplace(module.id);
            }
            AddAll(provides[i], module.providesFeatures);
            AddAll(needs[i], module.requiresModules);
            AddAll(needs[i], module.requiresFeatures);
            AddAll(needs[i], module.optionalModules);
            AddAll(needs[i], module.optionalFeatures);
        }
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

    // Kahn's algorithm, draining zero-indegree nodes in ascending package-id order
    // for determinism. Any nodes left in a cycle are appended by package-id order.
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
            // Cycle: append the remaining nodes in package-id order and stop.
            std::vector<std::size_t> rest;
            for (std::size_t i = 0; i < n; ++i)
            {
                if (!done[i])
                {
                    rest.push_back(i);
                }
            }
            std::ranges::sort(rest, [&](std::size_t a, std::size_t b) { return ids[a] < ids[b]; });
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
