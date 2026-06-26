# FrameLift Plugin SDK

Build plugins for [FrameLift](https://github.com/framelift/framelift) — a lightweight video player. A
plugin ships as one runtime-loaded Qt plugin DLL/SO with one `IModule` entry object and JSON-authored
plugin metadata compiled in by CMake.

The SDK keeps host implementation dependencies out of plugins: building a plugin
needs a C++23 compiler, CMake, and Qt 6. No legacy rendering or JSON libraries
are required — the host↔plugin boundary is a COM-like binary ABI (pure abstract
interfaces, POD-only signatures, C entry points), so a plugin built with any
compatible Windows compiler interoperates with the host regardless of how the
host was built.

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
    MyPlugin.cpp
    ${FRAMELIFT_SDK_SOURCES})
```

```sh
cmake -B build
cmake --build build
# -> build/plugins/example.my_plugin.dll
```

Drop the resulting plugin DLL/SO into the `plugins/` directory next to `framelift.exe` and it loads on
next launch. Plugins default to enabled; to stop one loading, set `<plugin-id>=disabled` in
`plugins.ini` in the FrameLift config directory.

## Writing a plugin

`MyPlugin.h`:

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
    .qml = false,
})
```

`MyPlugin.cpp`:

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
    MyPlugin.cpp
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
  "enabled": true,
  "provides": { "features": ["example.my_plugin"] },
  "requires": { "plugins": [], "features": [] },
  "optional": { "plugins": [], "features": [] },
  "platforms": []
}
```

### Umbrella headers

| Header | Provides |
|--------|----------|
| `<framelift/core.h>`     | module entry macro, module lifecycle, `ModuleBase`, context, ABI, events, hotkeys, `Log` |
| Qt/QML                   | QObject view models and plugin-embedded Qt Quick components |
| `<framelift/services.h>` | host + cross-plugin service interfaces (`IHistory`, `ISettingsStore`, `ISettingsRegistry`, `IPluginCatalog`, `IAppPaths`) |
| `<framelift/platform.h>` | media playback family (`IMediaPlayback`, `IMediaProperties`, `IVideoOutput`, `IAudioControl`, `ISubtitleControl`), window family (`IAppWindow`, `IGraphicsSurface`, `IEventPump`), `IFileDialog` |

### Module Entry

`FRAMELIFT_MODULE_ENTRY(Type, { ... })` belongs in the module header after the entry class declaration.
It takes the module entry type and a small runtime
`FrameLiftModuleEntryDesc` initializer. Plugin identity, file format version, plugin version, ABI,
features, dependencies, and platforms come from the JSON metadata compiled by CMake:

```cpp
FRAMELIFT_MODULE_ENTRY(MyModule, {
    .renderOrder = 50,                    // QML layer order; lower draws first / further back
})
```

QML is enabled by default, so the entry type must inherit `QObject` and the plugin
target must pass `QML_ENTRY` to `add_framelift_plugin`. Service-only modules opt out
with `.qml = false`; in that case `renderOrder` is ignored.

The macro emits a Qt plugin factory implementing `IPlugin`. The host reads the
embedded JSON metadata without instantiating the plugin, rejects incompatible ABI
versions, resolves plugin dependencies, and only then creates the module object and
their QObject/QML surfaces.

### Settings & keybinds

Settings are explicit per module: override `LoadSettings`/`SaveSettings` and
surface any Settings UI through a plugin-owned QML page registered with the
settings page registry. Keybinds still use descriptor tables so the host can load,
register, and bind them consistently:

```cpp
class MyModule : public QObject, public ModuleBase
{
protected:
    void LoadSettings(IModuleSettings& ps) override
    {
        maxEntries_ = ps.GetInt("maxEntries", 200);
    }

    void SaveSettings(IModuleSettings& ps) override
    {
        ps.SetInt("maxEntries", maxEntries_);
    }

    std::vector<framelift::Keybind> Keybinds() override
    {
        return {{"Toggle surface", "toggleSurface", &toggleKey_, "P",
                 [this] { ToggleSurface(); }}};
    }

private:
    int maxEntries_ = 200;
    std::string toggleKey_ = "P";
};
```

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
`ModuleBase` hook (`OnInstall`, `HandleEvent`, `HandleMediaEvent`, ...), a
helper-registered lambda (`framelift::Subscribe`, `framelift::Bind`, `framelift::AddItem`),
or the plugin constructor is
logged via `Log::Error` and swallowed with a safe fallback — the plugin
misbehaves loudly instead of crashing the host. Only code that implements `IModule`
raw, bypassing the scaffolding, keeps terminate-on-throw semantics.

### Native backend access (escape hatches)

The curated interfaces cover the common cases; for anything beyond them the raw
platform objects are reachable — bring the matching headers/libraries yourself:

- `IAppWindow::GetNativeHandle()` — the raw Qt window object pointer.

(The graphics API behind the window — OpenGL or Vulkan — is an internal detail and is
no longer exposed to plugins; the host owns all video/UI rendering.)

## ABI compatibility

The ABI is a single integer, `FRAMELIFT_ABI_VERSION` in `<framelift/ModuleABI.h>` — not a
`major.minor.patch` tuple. Each plugin declares its load-time contract with
`"abi": N` in `[Plugin].Plugin.json`; CMake validates that value against the SDK headers and
embeds it into the Qt plugin metadata. Before touching a vtable the host loads a plugin only when
`plugin.abiVersion == host.abiVersion` — an exact match, because host and plugins are built in
lockstep, so a mismatch means a stale binary to rebuild rather than a version to negotiate.

Bump the version only on a break to the core handshake: the Qt `IPlugin` factory,
plugin metadata layout, a host-*called* interface (`IModule`), or the bootstrap
surface of `IModuleContext`. New host capabilities are **not**
breaks — they ship as new service interfaces a plugin discovers with `ctx.GetService<T>()`, so
they never bump the version.

`find_package(FrameLiftSdk)` is gated on the ABI version (`ExactVersion`), so a mismatched SDK
fails at configure time. Settings, logging, and all cross-plugin data are exchanged through the
discoverable service interfaces and POD types — never by sharing C++ standard-library types
across the DLL boundary.

## License

[zlib](LICENSE)
