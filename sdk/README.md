# FrameLift Plugin SDK

Build plugins for [FrameLift](https://github.com/framelift/framelift) — a lightweight video player — as
standalone DLLs that the host loads at runtime from its `Plugins/` directory.

The SDK is **dependency-free**: building a plugin needs only a C++23 compiler and
CMake. No imgui, spdlog, stb, or JSON libraries are required — the host↔plugin
boundary is a COM-like binary ABI (pure abstract interfaces, POD-only signatures,
C entry points), so a plugin built with any compatible Windows compiler
interoperates with the host regardless of how the host was built.

## Layout

```
framelift-sdk-<ver>/
├── CMakeLists.txt          # standalone build root (builds the example)
├── cmake/
│   ├── FrameLiftSdk.cmake       # add_framelift_plugin() + the FrameLiftSdk target
│   ├── FrameLiftSdkConfig.cmake # find_package(FrameLiftSdk) entry point
│   └── FrameLiftSdkConfigVersion.cmake
├── include/framelift/           # public headers (umbrella: core.h, ui.h, services.h, platform.h)
├── src/                    # SDK helper sources, compiled into your plugin
├── examples/HelloPlugin/   # minimal worked example
├── README.md
└── LICENSE
```

## Quick start

```sh
# Build the bundled example plugin:
cmake -B build
cmake --build build
# → build/Plugins/HelloPlugin.dll
```

Drop the resulting DLL into the `Plugins/` directory next to `FrameLift.exe`, then add
its name to the `[plugins] enabled=` list in the FrameLift config, and it loads on
next launch.

## Writing a plugin

`MyPlugin.cpp`:

```cpp
#include <framelift/core.h>

class MyPlugin : public PluginBase
{
protected:
    const char* PluginName() const override { return "MyPlugin"; }
    void OnInstall(IPluginContext& ctx) override
    {
        Log::Info("[MyPlugin] hello from the FrameLift SDK!");
    }
};

// .name and .version are required; .render = false because MyPlugin draws nothing
FRAMELIFT_PLUGIN_EXPORT(MyPlugin, {
    .name = "MyPlugin",
    .version = {1, 0, 0},
    .render = false,
})
```

`CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.28)
project(MyPlugin LANGUAGES CXX)
find_package(FrameLiftSdk REQUIRED PATHS "/path/to/framelift-sdk/cmake" NO_DEFAULT_PATH)
add_framelift_plugin(MyPlugin MyPlugin.cpp ${FRAMELIFT_SDK_SOURCES})
```

### Umbrella headers

| Header | Provides |
|--------|----------|
| `<framelift/core.h>`     | plugin lifecycle, `PluginBase`, context, ABI, events, hotkeys, `Log` |
| `<framelift/ui.h>`       | `IRenderable`, `Panel`, `UIContext`, widgets |
| `<framelift/services.h>` | cross-plugin service interfaces (currently `IHistory`; communication is events-first) |
| `<framelift/platform.h>` | `IMediaPlayer`, `IAppWindow`, `IDirWatcher` |

### Export macro

`FRAMELIFT_PLUGIN_EXPORT(Type, { ... })` takes the plugin type and a braced
`FrameLiftPluginDesc` initializer (designated initializers, in declaration order):

```cpp
FRAMELIFT_PLUGIN_EXPORT(MyPanel, {
    .name = "MyPanel",                    // required
    .version = {1, 0, 0},                 // required — the plugin's own semver
    .renderOrder = 50,                    // draw order; lower draws first / further back
    .publisher = "Acme",                  // optional (nullptr when omitted)
    .description = "Does a thing",        // optional (nullptr when omitted)
})
```

`render` defaults to `true`, so a rendering plugin never mentions it — but a type
that does not implement `IRenderable` fails to compile until it states
`.render = false` (or derives `IRenderable`). `renderOrder` is ignored when
rendering is disabled.

The macro bakes a `framelift_plugin_info()` export carrying the plugin's name, its own
semver, and the ABI it was built against — read by the host before any vtable is
touched.

### Declarative settings & keybinds

Instead of hand-writing `LoadSettings`/`SaveSettings` and the keybind
load→register→bind dance, return descriptor tables over your members
(`<framelift/PluginFields.h>`, included by `<framelift/core.h>`). `PluginBase`'s default
hooks consume them — persistence, the Settings → Keybinds page row, and the
hotkey binding all come from one declaration:

```cpp
class MyPanel : public PluginBase
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
default for that leg only; call the `PluginBase::` version to keep it and add
extras.

### Media events

Override `PluginBase::HandleMediaEvent(const MediaEvent&)` to react to the player.
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
`PluginBase` hook (`OnInstall`, `HandleEvent`, `HandleMediaEvent`, …), a
`SafeRenderable`/`Panel` render, a helper-registered lambda (`framelift::Subscribe`,
`framelift::Bind`, `framelift::AddItem`, settings pages), or the plugin constructor is
logged via `Log::Error` and swallowed with a safe fallback — the plugin
misbehaves loudly instead of crashing the host. Only code that implements
`IPlugin`/`IRenderable` raw, bypassing the scaffolding, keeps
terminate-on-throw semantics.

### Native backend access (escape hatches)

The curated interfaces cover the common cases; for anything beyond them the raw
platform objects are reachable — bring the matching headers/libraries yourself:

- `IAppWindow::GetNativeHandle()` — the raw `SDL_Window*`.

(The graphics API behind the window — OpenGL or Vulkan — is an internal detail and is
no longer exposed to plugins; the host owns all video/UI rendering.)

## ABI compatibility

The ABI is versioned `major.minor.patch` (`FRAMELIFT_PLUGIN_ABI_*` in
`<framelift/PluginABI.h>`), reported by each plugin via `framelift_plugin_info()`. Before
touching a vtable the host loads a plugin only when
`plugin.major == host.major && plugin.minor <= host.minor`:

- **major** bumps on breaking changes (and any addition to a host-*called* plugin
  interface) — old plugins are rejected;
- **minor** bumps on backward-compatible additions to host-*provided* surface
  (a new context method or service) — old plugins keep loading;
- **patch** is ABI-neutral; carried and logged but never gates the load.

`find_package(FrameLiftSdk)` is gated on the ABI major version (`SameMajorVersion`), so
a mismatched-major SDK fails at configure time; the minor rule is enforced by the
runtime loader. Settings, logging, and all cross-plugin data are exchanged through
the context's typed getters and POD interfaces — never by sharing C++
standard-library types across the DLL boundary.

## License

[zlib](LICENSE)
