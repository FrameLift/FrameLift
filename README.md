# FrameLift

An **extensible** video player built on Dear ImGui, SDL3, FFmpeg, and libass — where user-facing
features are runtime-loaded plugins. The host application has no compile-time knowledge of any of
them: playback controls, playlists, history, settings, network streaming, and updates are all plugin
DLLs loaded at startup over a stable, versioned binary ABI; the window, decode/playback engine, and
platform integration are built-in modules compiled into the host. Add or remove plugin features by
dropping a DLL in or out of the `packages/` folder.

## Features

- **Plugin architecture** — user-facing features are separate DLLs loaded at runtime; build and ship your
  own against the dependency-free [Plugin SDK](#extending-framelift-plugin-sdk)
- **Vulkan-first rendering** — defaults to a Vulkan backend, automatically falling back to OpenGL
  on machines without a suitable Vulkan device
- **Zero-copy GPU decode** — on the Vulkan backend, supported codecs decode straight into GPU images
  the renderer samples directly (no CPU readback). Otherwise hardware decode via CUDA/NVDEC, D3D11VA
  (Windows), or VAAPI (Linux), with a software fallback last
- **Video & image playback** — MP4, MKV, AVI, MOV, WMV, FLV, WebM, M4V, MPG/MPEG, PNG, JPG, GIF, BMP,
  WebP, and anything FFmpeg supports
- **Network streaming** — play remote http/https/rtsp sources (and custom encrypted streams)
- **Playlist** — automatically populated from the opened file's directory, with optional recursive
  subdirectory scanning, shuffle, and auto-reload on directory changes
- **History with resume** — recently played files with saved positions, in a searchable side panel
- **Multi-track audio & subtitles** — switch tracks and adjust subtitle delay at runtime
- **Dynamic audio normalization** — optional dynaudnorm filter with tunable parameters
- **Image slideshow** — configurable auto-advance interval for image-only or mixed playlists
- **Fully rebindable hotkeys** — every action has a default keybind, all overridable in settings
- **Theming** — dark/light/classic presets, custom accent color, and font
- **Auto-update** — background update check against GitHub Releases, applied on next launch (Windows)
- **Debug & benchmark overlays** — live playback stats and performance benchmarking

## Installation

Download the latest release archive from the [Releases](../../releases) page, extract, and run
`FrameLift`. No installer needed — the archive is self-contained.

**Platforms:** Windows (x86_64) and Linux (X11/Wayland via SDL3).

**Requirements:** a GPU supporting **Vulkan 1.1+** *or* **OpenGL 3.3** (FrameLift auto-selects Vulkan
and falls back to OpenGL when needed; zero-copy GPU decode additionally needs a Vulkan 1.3 video-decode
capable GPU). On Linux, install SDL3, FFmpeg, and libass from your distribution (e.g. `libsdl3-0`,
`libavcodec`, `libavformat`, `libavfilter`, `libass9`) plus a Vulkan loader (`libvulkan1`).

## Configuration

Settings live in `settings.ini` in the platform config directory and are edited through the in-app
**Settings menu** (Ctrl+,) — no need to hand-edit the file. Among the things you can configure:

- **Rendering & decode** — graphics backend (Vulkan/OpenGL, applied on restart) and hardware decoding
- **Playback** — precise seeking, video-sync, automatic loading of matching subtitle/audio files
- **Playlist** — recursive subdirectory scanning and depth, mixed video/image playlists, slideshow
  duration, and directory auto-reload
- **History** — how many recently played entries to keep
- **Audio** — dynamic normalization parameters
- **Appearance** — theme preset, accent color, and UI font
- **Hotkeys** — every action is rebindable
- **File types** — the video and image extension lists are fully customizable

Each plugin persists its own settings in its own INI section. See the Settings menu for the complete,
current list of options.

## Extending FrameLift (Plugin SDK)

Each FrameLift plugin ships as a **package** — one runtime-loaded DLL under the `packages/` folder
next to the executable. A *plugin* (what you build with the SDK) ships as a package: a `.Plugin.json`
plus its `.Module.json`(s), compiled into one DLL that carries one or more **modules**, each declaring
the **features** it provides and requires. A package may carry several modules, and each module can be
enabled or disabled independently from the Settings → Plugins page (persisted per module id in
`packages.ini`). You can build your own against the **dependency-free plugin SDK**
(`framelift-sdk-<ver>.zip`, published as a Release asset on every version tag).

- **Stable binary boundary.** The host↔plugin boundary is a COM-like binary ABI: pure abstract
  interfaces, POD-only method signatures, and `extern "C"` entry points. A plugin built with any
  compatible Windows compiler loads regardless of how the host was built. The ABI is a single integer,
  `FRAMELIFT_ABI_VERSION`, matched exactly; new host capabilities arrive as new discoverable service
  interfaces (capability discovery), so they never bump it.
- **JSON-authored metadata.** Plugin packages declare package/module/feature metadata in
  `[Plugin].Plugin.json` and `[Module].Module.json` files, including `fileVersion`, package `version`,
  and `abi`. CMake validates those files and embeds the resulting POD metadata into the
  plugin DLL; runtime does not read JSON sidecars.
- **Small surface.** Plugins include only the umbrella headers `<framelift/core.h>`, `<framelift/ui.h>`,
  `<framelift/services.h>`, and `<framelift/platform.h>` — never host internals.
- **Cross-plugin communication.** Plugins never link against each other; they interact through the
  module context via pub/sub events and capability services discovered with `ctx.GetService<T>()` —
  `IHistory`, the settings split (`ISettingsStore`/`ISettingsRegistry`), and platform interface
  families (`IMediaPlayback`/`IAudioControl`/…, `IAppWindow`/`IGraphicsSurface`/`IEventPump`, …).

See [sdk/](sdk/) for the API, and the [FrameLift-Examples](https://github.com/FrameLift/FrameLift-Examples)
repository for worked example plugins to copy from.

## Repository Layout

```
FrameLift/
├── sdk/                    # Public plugin SDK — everything a plugin author needs
│   ├── include/framelift/  # Public headers (include path: sdk/include)
│   │   ├── core.h          # Umbrella: module lifecycle, context, ABI, events, hotkeys
│   │   ├── ui.h            # Umbrella: IRenderable, Panel, UIContext, widgets
│   │   ├── services.h      # Umbrella: cross-plugin service interfaces
│   │   ├── platform.h      # Umbrella: media playback + window interface families, IDirWatcher, IFileDialog
│   │   ├── services/       # Per-plugin service interfaces (IHistory, …)
│   │   ├── platform/       # Platform service interfaces
│   │   └── ui/             # UI helper headers (Panel, UIContext, Widgets, …)
│   └── src/                # SDK helper sources compiled into each plugin
│       # (worked examples live in the separate FrameLift-Examples repo)
│
├── src/                    # Host entry point (framelift.exe): App, CLI, main — owns only the loop
│
├── modules/                # Built-in modules compiled into the host (not shipped as DLLs)
│   ├── host/               # playback, audio, settings, services, controls, fonts, read-ahead, ui, logging, module-runtime
│   ├── gfx/                # Graphics backends (graphics-core, opengl, vulkan) and video renderers
│   └── platform/           # SDL3 window (window-sdl) and per-OS dir watchers (dir-watch)
│
├── plugins/                # Plugins — each builds one package (DLL) emitted into packages/
│   ├── overlay/            # Playback controls, idle screen, notifications
│   ├── playlist/           # Folder playlist and file navigation
│   ├── history/            # Recently played + resume positions
│   ├── settings-menu/      # Settings and keybind editor
│   ├── debug-overlay/      # Live playback stats
│   ├── benchmark/          # Performance / system-stats benchmark overlay
│   ├── remote-stream/      # Remote network streams (http/https/rtsp + encrypted)
│   ├── context-menu/       # Shared right-click context-menu service
│   └── updater/            # Auto-update (Windows-only)
│
├── cmake/                  # Dependency fetch, shader compile, and SDK packaging modules
├── vcpkg.json              # Windows native libs (SDL3, FFmpeg, libass) — vcpkg manifest
└── CMakeLists.txt
```

The host (`src/`) has no compile-time knowledge of specific plugins; every package is loaded at
runtime from a DLL such as `packages/framelift.playlist.core.dll`, dependency-resolved and ABI-checked
before any module object is constructed. Each module a package carries is instantiated unless its id is
disabled in `packages.ini` (one `framelift.playlist.core=enabled|disabled` row per module; absent ⇒
enabled).

## Building from Source

### Prerequisites

- CMake 4.2.0+
- C++23-capable compiler (GCC, Clang, or MSVC)
- `glslang` on PATH (the `glslang-tools` package) — compiles the GLSL shaders to SPIR-V at build time.
  CMake fetches and builds glslang as a fallback only when it isn't found on PATH.
- `clang-format` on PATH (for the pre-commit hook)
- **Linux only:** development packages for SDL3, FFmpeg, libass, Vulkan, and OpenGL
  (e.g. on Ubuntu 25.04+: `sudo apt install libsdl3-dev libavformat-dev libavcodec-dev libavutil-dev
  libswscale-dev libswresample-dev libavfilter-dev libass-dev libvulkan-dev libgl1-mesa-dev
  glslang-tools pkg-config`).
- **Windows only:** [vcpkg](https://vcpkg.io) (`VCPKG_ROOT` set) — SDL3, FFmpeg, and libass are
  resolved from the manifest in `vcpkg.json`. Configure with the vcpkg preset (`CMakePresets.json`).

### Steps

```sh
git clone <repo-url>
cd FrameLift

# Activate formatting hook (one-time per clone)
git config core.hooksPath .github/hooks

# Configure and build
cmake -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-debug --config Debug
```

Output: `cmake-build-debug/framelift` (`.exe` on Windows). On Windows the required shared libraries
are copied next to the executable; on Linux SDL3/FFmpeg/libass are resolved from the system. Package
DLLs are placed under `cmake-build-debug/packages/` (`.dll` on Windows, `.so` on Linux).
The auto-updater package declares Windows-only platform support and is disabled automatically elsewhere.
The Vulkan and OpenGL loaders are resolved at
runtime (no link-time `libvulkan` dependency), so only a GPU driver is needed to run.

#### Lean builds

Each built-in module exposes a `FRAMELIFT_MODULE_<NAME>` CMake option whose default is the module's
`.Module.json` `enabled` flag. Pass `-DFRAMELIFT_MODULE_<NAME>=OFF` to drop a module and its
dependencies from the build — for example, a Vulkan-free build that keeps only the OpenGL backend:

```sh
cmake -B cmake-build-lean -DCMAKE_BUILD_TYPE=Debug -DFRAMELIFT_MODULE_GRAPHICS_VULKAN=OFF
cmake --build cmake-build-lean
```

CMake prints the enabled/disabled module table at configure time. Modules unsupported on the current
platform, or marked `required`, cannot be toggled this way.

### Dependencies

Native media libraries come from vcpkg (Windows) or the system (Linux); everything else is fetched by
CMake via `FetchContent`.

| Library          | Version           | Source        |
|------------------|-------------------|---------------|
| SDL3             | vcpkg / system    | vcpkg/system  |
| FFmpeg           | vcpkg / system    | vcpkg/system  |
| libass           | vcpkg / system    | vcpkg/system  |
| Dear ImGui       | 1.92.8-docking    | FetchContent  |
| spdlog           | 1.17.0            | FetchContent  |
| nlohmann/json    | 3.11.3            | FetchContent  |
| stb              | master            | FetchContent  |
| Vulkan-Headers   | 1.4.354           | FetchContent  |
| volk             | 1.4.350           | FetchContent  |
| VulkanMemoryAllocator | 3.4.0        | FetchContent  |
| glslang          | 15.0.0 (build-time, fallback) | PATH / FetchContent |

## License

[zlib](LICENSE)
