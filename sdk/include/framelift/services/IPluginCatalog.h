#pragma once
#include <framelift/ModuleABI.h>

// The plugin catalogue: enumerate every plugin discovered in the plugins/
// directory (loaded or present-but-disabled) and toggle plugins. A capability
// service — discover it with ctx.GetService<IPluginCatalog>().
class IPluginCatalog
{
public:
    static constexpr const char* InterfaceId = "framelift.IPluginCatalog";
    virtual ~IPluginCatalog() = default;

    // Per-plugin identity, in catalogue order.
    //   pluginId    — the plugin's load key
    //   displayName — human-readable plugin name (equals pluginId if unnamed)
    //   version     — pointer to {major, minor, patch}; {0,0,0} if metadata unreadable
    //   publisher   — vendor/author, or "" if unset
    //   description — one-line summary, or "" if unset
    //   enabled     — persisted enable state (live; reflects toggles immediately)
    //   loaded      — instantiated this session
    //   loadFailed  — true iff it was enabled at startup yet did not load
    // Every const char* (and the version pointer) is valid ONLY for the duration of
    // this call — copy anything you need; never cache the pointers.
    virtual void EnumeratePlugins(
        void (*visit)(
            const char* pluginId, const char* displayName, const int* version, const char* publisher,
            const char* description, bool enabled, bool loaded, bool loadFailed, void* visitUd
        ),
        void* visitUd
    ) const noexcept = 0;

    // Add/remove a single plugin from the persisted enabled list and save. Unknown
    // ids are ignored. The change takes effect on the next application start.
    virtual void SetPluginEnabled(const char* pluginId, bool enabled) noexcept = 0;
};
