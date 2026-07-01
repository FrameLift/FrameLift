#pragma once

#include <map>
#include <string>
#include <unordered_set>
#include <vector>

// User-editable plugin enablement manifest (pref-dir plugins.ini), independent of
// the typed Settings. Opt-out semantics: every plugin in plugins/ loads unless the
// user explicitly disables it here, so dropping in a third-party DLL works with no
// edit. One plugin per row, keyed by plugin id:
//
//   framelift.overlay=disabled
//   framelift.playlist=enabled
//
// A plugin id absent from the file defaults to enabled.
class PluginConfig
{
public:
    // Parse "id=enabled|disabled" rows. A missing file leaves the manifest empty
    // (everything enabled).
    void Load(const std::string& path);

    // Write every known state, one sorted row per plugin, with a comment header.
    void Save(const std::string& path) const;

    [[nodiscard]] bool IsEnabled(const std::string& id) const
    {
        const auto it = states_.find(id);
        return it == states_.end() ? true : it->second;
    }

    // Plugin ids explicitly disabled — handed to the loader to skip.
    [[nodiscard]] std::unordered_set<std::string> DisabledIds() const;

    void Set(const std::string& id, bool enabled)
    {
        states_[id] = enabled;
    }

    // Record any not-yet-known id as enabled so the saved file is a complete,
    // hand-editable manifest of the current plugin set.
    void EnsureKnown(const std::vector<std::string>& ids)
    {
        for (const auto& id : ids)
        {
            states_.emplace(id, true);
        }
    }

private:
    std::map<std::string, bool> states_; // plugin id -> enabled (sorted for deterministic save)
};
