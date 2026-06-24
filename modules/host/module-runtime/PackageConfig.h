#pragma once

#include <map>
#include <string>
#include <unordered_set>
#include <vector>

// User-editable module enablement manifest (pref-dir packages.ini), independent of
// the typed Settings. Opt-out semantics: every module carried by a package in
// packages/ loads unless the user explicitly disables it here, so dropping in a
// third-party DLL works with no edit. One module per row, keyed by MODULE id (a
// package may carry several, each toggled independently):
//
//   framelift.overlay.core=disabled
//   framelift.playlist.core=enabled
//
// A module id absent from the file defaults to enabled.
class PackageConfig
{
public:
    // Parse "id=enabled|disabled" rows. A missing file leaves the manifest empty
    // (everything enabled).
    void Load(const std::string& path);

    // Write every known state, one sorted row per module, with a comment header.
    void Save(const std::string& path) const;

    [[nodiscard]] bool IsEnabled(const std::string& id) const
    {
        const auto it = states_.find(id);
        return it == states_.end() ? true : it->second;
    }

    // Module ids explicitly disabled — handed to the loader to skip.
    [[nodiscard]] std::unordered_set<std::string> DisabledIds() const;

    void Set(const std::string& id, bool enabled)
    {
        states_[id] = enabled;
    }

    // Record any not-yet-known id as enabled so the saved file is a complete,
    // hand-editable manifest of the current module set.
    void EnsureKnown(const std::vector<std::string>& ids)
    {
        for (const auto& id : ids)
        {
            states_.emplace(id, true);
        }
    }

private:
    std::map<std::string, bool> states_; // module id → enabled (sorted for deterministic save)
};
