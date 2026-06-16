#pragma once

// ── Plugin ABI version (major.minor.patch) ────────────────────────────────────
//
// The host loads a plugin iff:
//     plugin.abiMajor == host.abiMajor  &&  plugin.abiMinor <= host.abiMinor
// (patch never affects the decision — see below). The check runs before any
// vtable is touched.
//
//   MAJOR — bump on any *breaking* change: removing/reordering virtual methods,
//           changing a signature, or appending to a host-CALLED plugin interface
//           (IPlugin, IRenderable) or a framelift_* export signature — the host would
//           call a new vtable slot on an older, shorter plugin vtable. Reset
//           MINOR and PATCH to 0.
//   MINOR — bump on backward-compatible *additions* to host-PROVIDED surface:
//           appending a method to IPluginContext, adding a new service interface,
//           adding a new optional export. Old plugins (lower minor) still load.
//           Reset PATCH to 0.
//   PATCH — bump on internal, ABI-neutral fixes. Carried and logged but NOT
//           gated; reserved as metadata for the future plugin repository to
//           detect and push patched builds.
// MAJOR 2: the host↔player/window rendering hand-off was generalized for the Vulkan
// backend — IMediaPlayer::InitRender now takes an opaque graphics-backend handle (was
// a GL proc loader), IAppWindow drops GetGLProcAddr and gains BeginFrame/GetGraphicsBackend.
#define FRAMELIFT_PLUGIN_ABI_MAJOR 2
#define FRAMELIFT_PLUGIN_ABI_MINOR 0
#define FRAMELIFT_PLUGIN_ABI_PATCH 0

// POD identity + ABI descriptor a plugin exports via framelift_plugin_info(). The
// host reads it before constructing the plugin, so it must stay layout-stable:
// it is itself part of the ABI and only a MAJOR bump may change its layout. The
// abi fields come first so the loader can always read them regardless of any
// later (major-gated) additions.
struct FrameLiftPluginInfo
{
    int abiMajor;            // FRAMELIFT_PLUGIN_ABI_MAJOR the plugin was built against
    int abiMinor;            // FRAMELIFT_PLUGIN_ABI_MINOR  "
    int abiPatch;            // FRAMELIFT_PLUGIN_ABI_PATCH  " — informational, not gated
    const char* name;        // human-readable plugin name (static storage)
    int version[3];          // plugin's own product semver: {major, minor, patch}
    const char* publisher;   // optional — author/vendor name, nullptr if unset
    const char* description; // optional — one-line summary, nullptr if unset
};

// Load-time compatibility predicate. Header-only and POD so both the host loader
// and the unit tests share one source of truth. Patch is intentionally absent —
// a patch bump is non-breaking by definition and never changes the decision.
inline bool FrameLiftAbiCompatible(int pluginMajor, int pluginMinor, int hostMajor, int hostMinor) noexcept
{
    return pluginMajor == hostMajor && pluginMinor <= hostMinor;
}