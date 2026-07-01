#pragma once
#include "PluginMetadata.h"
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

struct PluginResolveCandidate
{
    const PluginMetadata* meta = nullptr;
};

struct PluginResolveDecision
{
    bool accepted = false;
    std::string reason;
};

[[nodiscard]] const char* FrameLiftCurrentPlatformId() noexcept;

[[nodiscard]] std::vector<PluginResolveDecision> ResolvePlugins(
    const std::vector<PluginResolveCandidate>& candidates, std::string_view platformId
);

// Return indices into `candidates` in a deterministic load order: any plugin that
// provides a plugin id/feature listed in another plugin's requires/optional is ordered
// before that consumer (so service providers Install first). Ties — and any nodes in
// a dependency cycle — fall back to ascending plugin-id order.
[[nodiscard]] std::vector<std::size_t> OrderPlugins(const std::vector<PluginResolveCandidate>& candidates);
