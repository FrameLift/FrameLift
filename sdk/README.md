# FrameLift Plugin SDK

Build plugins for [FrameLift](https://github.com/framelift/framelift) — a lightweight video player. A
plugin ships as a **package**: one runtime-loaded DLL that bundles one or more **modules**, with the
JSON-authored package/module metadata compiled in by CMake.

The SDK is **dependency-free**: building a plugin needs only a C++23 compiler and
CMake. No imgui, stb, or JSON libraries are required — the host↔plugin
boundary is a COM-like binary ABI (pure abstract interfaces, POD-only signatures,
C entry points), so a plugin built with any compatible Windows compiler
interoperates with the host regardless of how the host was built.

## Layout

```
framelift-sdk-<ver>/
├── CMakeLists.txt          # skeleton build root (declare your plugin here)
├── cmake/
│   ├── FrameLiftSdk.cmake       # add_framelift_plugin() + the FrameLiftSdk target
│   ├── FrameLiftSdkConfig.cmake # find_package(FrameLiftSdk) entry point
│   └── FrameLiftSdkConfigVersion.cmake
├── include/framelift/           # public headers (umbrella: core.h, ui.h, services.h, platform.h)
├── src/                    # SDK helper sources, compiled into your plugin
├── README.md
└── LICENSE
```

Worked example plugins live in the separate
[FrameLift-Examples](https://github.com/FrameLift/FrameLift-Examples) repository — clone it for a
runnable starting point you can copy from.

## Quick start

The archive root is a skeleton `CMakeLists.txt`: it runs `find_package(FrameLiftSdk)` against the
SDK shipped beside it, then you declare your plugin (see [Writing a plugin](#writing-a-plugin)):

```cmake
add_framelift_plugin(MyPlugin
    PLUGIN_JSON "${CMAKE_CURRENT_SOURCE_DIR}/MyPlugin.Plugin.json"
    core/MyPlugin.cpp
    ${FRAMELIFT_SDK_SOURCES})
```

```sh
cmake -B build
cmake --build build
# -> build/packages/<publisher>.<package>.<module>.dll
```

Drop the resulting package DLL into the `packages/` directory next to `framelift.exe` and it loads on
next launch. Modules default to enabled; to stop one loading, set `<module-id>=disabled` in
`packages.ini` in the FrameLift config directory.

## Writing a plugin

`core/MyPlugin.h`:

```cpp
#pragma once
#include <framelift/core.h>

class MyPlugin : public ModuleBase
{
protected:
    const char* ModuleName() const override { return "MyPlugin"; }
    void OnInstall(IModuleContext& ctx) override;
};

FRAMELIFT_MODULE_ENTRY(MyPlugin, {
    .render = false,
})
```

`core/MyPlugin.cpp`:

```cpp
#include "MyPlugin.h"

void MyPlugin::OnInstall(IModuleContext&)
{
    Log::Info("[MyPlugin] hello from the FrameLift SDK!");
}
```

`CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.28)
project(MyPlugin LANGUAGES CXX)
find_package(FrameLiftSdk REQUIRED PATHS "/path/to/framelift-sdk/cmake" NO_DEFAULT_PATH)
add_framelift_plugin(MyPlugin
    PLUGIN_JSON "${CMAKE_CURRENT_SOURCE_DIR}/MyPlugin.Plugin.json"
    core/MyPlugin.cpp
    ${FRAMELIFT_SDK_SOURCES})
```

`MyPlugin.Plugin.json`:

```json
{
  "fileVersion": 1,
  "id": "example.my_plugin",
  "name": "MyPlugin",
  "publisher": "Acme",
  "description": "Does a thing",
  "version": "1.0.0",
  "abi": 1,
  "modules": ["core/Core.Module.json"]
}
```

`core/Core.Module.json`:

```json
{
  "fileVersion": 1,
  "id": "example.my_plugin.core",
  "name": "My Plugin Core",
  "description": "Main runtime module.",
  "enabled": true,
  "provides": { "features": ["example.my_plugin"] },
  "requires": { "modules": [], "features": [] },
  "optional": { "modules": [], "features": [] },
  "platforms": []
}
```

### Packages with several modules

One package DLL can carry more than one module — useful for a large plugin whose pieces should be
toggled independently. List every `.Module.json` in the `.Plugin.json` `modules` array, then register
each entry type against its module id (the ids must match the JSON):

```cpp
FRAMELIFT_PACKAGE_BEGIN()
  FRAMELIFT_MODULE("example.my_plugin.core",  MyPluginCore,  { .renderOrder = 50 })
  FRAMELIFT_MODULE("example.my_plugin.tools", MyPluginTools, { .render = false })
FRAMELIFT_PACKAGE_END()
```

Each module shows up as its own toggle under the package on the Settings → Plugins page, persisted by
module id in `packages.ini`. Artifact names are lowercase: a multi-module package DLL is named
`publisher.package` (no module suffix); a single-module one is `publisher.package.module`.
`FRAMELIFT_MODULE_ENTRY` is the single-module shorthand for the block above.

### Umbrella headers

| Header | Provides |
|--------|----------|
| `<framelift/core.h>`     | module entry macro, module lifecycle, `ModuleBase`, context, ABI, events, hotkeys, `Log` |
| Qt/QML                   | QObject view models and package-embedded Qt Quick components |
| `<framelift/services.h>` | host + cross-plugin service interfaces (`IHistory`, `ISettingsStore`, `ISettingsRegistry`, `IPackageCatalog`, `IFontCatalog`, `IAppPaths`) |
| `<framelift/platform.h>` | media playback family (`IMediaPlayback`, `IMediaProperties`, `IVideoOutput`, `IAudioControl`, `ISubtitleControl`), window family (`IAppWindow`, `IGraphicsSurface`, `IEventPump`), `IDirWatcher`, `IFileDialog` |

### Module Entry

`FRAMELIFT_MODULE_ENTRY(Type, { ... })` belongs in the module header after the entry class declaration.
It takes the module entry type and a small runtime
`FrameLiftModuleEntryDesc` initializer. Package identity, file format version, package version, ABI,
modules, features, dependencies, and platforms come from the JSON metadata compiled by CMake:

```cpp
FRAMELIFT_MODULE_ENTRY(MyPanel, {
    .renderOrder = 50,                    // draw order; lower draws first / further back
})
```

`render` defaults to `true`, so a rendering module never mentions it — but a type
that does not implement `IRenderable` fails to compile until it states
`.render = false` (or derives `IRenderable`). `renderOrder` is ignored when
rendering is disabled.

The macro bakes a `framelift_module_info()` export carrying the generated package/module metadata and
the ABI declared in JSON. The host reads it, rejects incompatible ABI versions, resolves package
dependencies, and only then creates the module object.

### Declarative settings & keybinds

Instead of hand-writing `LoadSettings`/`SaveSettings` and the keybind
load→register→bind dance, return descriptor tables over your members
(`<framelift/PluginFields.h>`, included by `<framelift/core.h>`). `ModuleBase`'s default
hooks consume them — persistence, the Settings → Keybinds page row, and the
hotkey binding all come from one declaration:

```cpp
class MyPanel : public ModuleBase
{
protected:
    std::vector<framelift::SettingsField> SettingsFields() override
    {
        return {{"maxEntries", &maxEntries_, 200}};
    }

    std::vector<framelift::Keybind> Keybinds() override
    {
        return {{"Toggle panel", "togglePanel", &toggleKey_, "P",
                 [this] { Toggle(); }}};
    }

private:
    int maxEntries_ = 200;
    std::string toggleKey_ = "P";
};
```

Overriding one of the hooks (e.g. `LoadSettings`) replaces the table-driven
default for that leg only; call the `ModuleBase::` version to keep it and add
extras.

### Media events

Override `ModuleBase::HandleMediaEvent(const MediaEvent&)` to react to the player.
The host decodes the FFmpeg backend's stream into a curated, ABI-stable `MediaEventType`
(`framelift/platform/IMediaPlayer.h`):

- Lifecycle: `StartFile`, `FileLoaded`, `PlaybackRestart` (fires after a seek
  completes), `Seek`, `EndFile`, `VideoReconfig`, `AudioReconfig`.
- `PropertyChange` — an observed `PlayerProperty` changed. The active value lives in
  `event.property.value`, tagged by `event.property.type`: `flag` / `dbl` / `i64`, or
  `str` (NUL-terminated, copied) when `type == PropertyType::String` — e.g. `Path`,
  `MediaTitle`, `HwDecCurrent`.
- `Other` — an event the host does not surface distinctly; safe to ignore.

```cpp
void HandleMediaEvent(const MediaEvent& e) override
{
    if (e.type == MediaEventType::FileLoaded) { /* tracks/metadata are ready */ }
    if (e.type == MediaEventType::PropertyChange &&
        e.property.prop == PlayerProperty::MediaTitle &&
        e.property.type == PropertyType::String)
    {
        Log::Info("[MyPlugin] now playing: {}", e.property.value.str);
    }
}
```

### Exceptions

The host↔plugin boundary is `noexcept` — an exception crossing it would be
undefined behavior. The SDK scaffolding catches plugin exceptions on the plugin
side of the boundary (`framelift::Guard` in `<framelift/Guard.h>`): a throw from a
`ModuleBase` hook (`OnInstall`, `HandleEvent`, `HandleMediaEvent`, …), a
`SafeRenderable`/`Panel` render, a helper-registered lambda (`framelift::Subscribe`,
`framelift::Bind`, `framelift::AddItem`, settings pages), or the plugin constructor is
logged via `Log::Error` and swallowed with a safe fallback — the plugin
misbehaves loudly instead of crashing the host. Only code that implements
`IModule`/`IRenderable` raw, bypassing the scaffolding, keeps
terminate-on-throw semantics.

### Native backend access (escape hatches)

The curated interfaces cover the common cases; for anything beyond them the raw
platform objects are reachable — bring the matching headers/libraries yourself:

- `IAppWindow::GetNativeHandle()` — the raw `SDL_Window*`.

(The graphics API behind the window — OpenGL or Vulkan — is an internal detail and is
no longer exposed to plugins; the host owns all video/UI rendering.)

## ABI compatibility

The ABI is a single integer, `FRAMELIFT_ABI_VERSION` in `<framelift/ModuleABI.h>` — not a
`major.minor.patch` tuple. Each plugin package declares its load-time contract with
`"abi": N` in `[Plugin].Plugin.json`; CMake validates that value against the SDK headers and
embeds it into `framelift_module_info()`. Before touching a vtable the host loads a plugin only
when `plugin.abiVersion == host.abiVersion` — an exact match, because host and plugins are built
in lockstep, so a mismatch means a stale binary to rebuild rather than a version to negotiate.

Bump the version only on a break to the core handshake: a `framelift_*` export, the
`FrameLiftPackageInfo`/`FrameLiftModuleInfo` layout, a host-*called* interface (`IModule`,
`IRenderable`), or the bootstrap surface of `IModuleContext`. New host capabilities are **not**
breaks — they ship as new service interfaces a plugin discovers with `ctx.GetService<T>()`, so
they never bump the version.

`find_package(FrameLiftSdk)` is gated on the ABI version (`ExactVersion`), so a mismatched SDK
fails at configure time. Settings, logging, and all cross-plugin data are exchanged through the
discoverable service interfaces and POD types — never by sharing C++ standard-library types
across the DLL boundary.

## License

[zlib](LICENSE)
