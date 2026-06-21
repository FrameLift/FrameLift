#pragma once

// Plugin ABI version — a single integer
//
// FrameLift ships the host and every plugin from one source tree, in lockstep, so
// the ABI gate only needs to catch a stale binary accidentally left behind. The
// host loads a plugin iff:
//     plugin.abiVersion == host.abiVersion
// The check runs before any vtable is touched.
//
// Bump the version ONLY on a break to the core load-bearing handshake (Tier 1):
//   - a framelift_* export signature,
//   - the FrameLiftPackageInfo / FrameLiftModuleInfo POD layout,
//   - a host-CALLED interface (IModule, IRenderable),
//   - the bootstrap surface of IModuleContext (GetServiceRaw/RegisterServiceRaw/
//     SubscribeRaw/PublishRaw).
//
// Everything else is a capability surface (Tier 2): host functionality is exposed
// as small, independently-discovered service interfaces. Adding, changing, or
// removing a Tier-2 interface NEVER bumps the version — consumers discover it with
// ctx.GetService<T>() and degrade when it returns nullptr.
#define FRAMELIFT_ABI_VERSION 1

struct FrameLiftStringList
{
    const char* const* items;
    int count;
};

struct FrameLiftModuleInfo
{
    const char* id;          // Stable dotted module id, e.g. "framelift.playlist.core".
    const char* name;        // Human-readable module name.
    const char* description; // One-line summary, nullptr if unset.
    FrameLiftStringList providesFeatures;
    FrameLiftStringList requiresModules;
    FrameLiftStringList requiresFeatures;
    FrameLiftStringList optionalModules;
    FrameLiftStringList optionalFeatures;
    FrameLiftStringList platforms; // Empty means every platform.
};

// POD identity + ABI descriptor exported by framelift_module_info(). The host
// reads it before constructing the module. The version comes from the JSON-authored
// abi value and appears first so the loader can read it before touching any later
// field — the only layout guarantee the host relies on across a version break.
struct FrameLiftPackageInfo
{
    int abiVersion;
    const char* packageId;   // Stable dotted package id, e.g. "framelift.playlist".
    const char* moduleFile;  // Shipped module binary basename, e.g. "framelift.playlist.core".
    const char* name;        // Human-readable package name.
    int version[3];          // Package version from JSON: {major, minor, patch}.
    const char* publisher;   // Optional author/vendor name, nullptr if unset.
    const char* description; // One-line summary, nullptr if unset.
    const FrameLiftModuleInfo* modules;
    int moduleCount;
};

// Load-time compatibility predicate. Header-only and POD so both the host loader
// and the unit tests share one source of truth. The rule is exact equality: host
// and plugins are built together, so any mismatch means a stale binary that must be
// rebuilt rather than loaded.
inline bool FrameLiftAbiCompatible(int pluginVersion, int hostVersion) noexcept
{
    return pluginVersion == hostVersion;
}
