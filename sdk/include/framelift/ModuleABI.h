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
//   - the IPlugin interface signature or its Q_PLUGIN_METADATA IID,
//   - the embedded plugin-metadata JSON shape the host parses,
//   - a host-CALLED interface (IModule),
//   - the bootstrap surface of IModuleContext (GetServiceRaw/RegisterServiceRaw/
//     SubscribeRaw/PublishRaw).
//
// Everything else is a capability surface (Tier 2): host functionality is exposed
// as small, independently-discovered service interfaces. Adding, changing, or
// removing a Tier-2 interface NEVER bumps the version — consumers discover it with
// ctx.GetService<T>() and degrade when it returns nullptr. QML view models are
// QObject instances returned by IPlugin and are therefore part of Tier 1.
#define FRAMELIFT_ABI_VERSION 1

// Load-time compatibility predicate. Header-only so both the host loader and the
// unit tests share one source of truth. The rule is exact equality: host and
// plugins are built together, so any mismatch means a stale binary that must be
// rebuilt rather than loaded.
inline bool FrameLiftAbiCompatible(int pluginVersion, int hostVersion) noexcept
{
    return pluginVersion == hostVersion;
}
