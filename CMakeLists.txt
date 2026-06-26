cmake_minimum_required(VERSION 4.2.0)

set(FRAMELIFT_VERSION "0.0.0" CACHE STRING "Numeric version passed to CMake project() — must be X.Y.Z")
project(FrameLift VERSION ${FRAMELIFT_VERSION} LANGUAGES C CXX)

# Build the native unit-test suite (tests/) as part of this project so it shows up
# in IDEs. Disabled for the Windows app/CI build, which can't run native test exes.
# Forced OFF during the Qt migration (the test targets reference SDL/ImGui-era headers).
option(FRAMELIFT_BUILD_TESTS "Build the unit tests (tests/)" OFF)

# Build runtime plugin package DLLs in plugins/. Packages load through QPluginLoader
# and embed their Qt Quick surfaces as QML modules.
option(FRAMELIFT_BUILD_PLUGINS "Build the plugin package DLLs (plugins/)" ON)

# Display string shown in the UI; defaults to the numeric version for local/release builds.
if (NOT DEFINED FRAMELIFT_VERSION_DISPLAY OR FRAMELIFT_VERSION_DISPLAY STREQUAL "")
    set(FRAMELIFT_VERSION_DISPLAY "${FRAMELIFT_VERSION}")
endif ()

include(FetchContent)

# Built-in host modules are JSON-authored and drive compile-time ownership.
include("${CMAKE_SOURCE_DIR}/cmake/FrameLiftBuiltinModules.cmake")
framelift_load_builtin_modules("${CMAKE_SOURCE_DIR}/modules")
framelift_report_builtin_modules()

# ── Platform libraries (FFmpeg + libass) ──────────────────────────────────────
# Defines the `ffmpeg` and `libass` targets: Linux uses system dev packages
# (libav*-dev / libass-dev); Windows resolves them from the vcpkg manifest
# (vcpkg.json). Qt6 (the window/audio/UI toolkit, replacing SDL3) is resolved
# separately via find_package(Qt6 ...) below.
include("${CMAKE_SOURCE_DIR}/cmake/FrameLiftPlatformLibs.cmake")

# ── OpenGL (opengl32 on Windows, libGL on Linux) ──────────────────────────────
find_package(OpenGL REQUIRED)

# ── Qt6 (window / events / audio / UI — replaces SDL3 + Dear ImGui) ───────────
# Gui+Quick drive the QQuickWindow and scene graph (video is a QSGRenderNode);
# Multimedia provides QAudioSink (replacing the SDL3 audio device). Widgets
# provides the native QFileDialog open-file picker (replacing SDL_ShowOpenFileDialog).
# AUTOMOC is required for the Q_OBJECT types in the window-qt module.
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Multimedia Widgets)
set(CMAKE_AUTOMOC ON)

# ── Host third-party dependencies (stb/Vulkan stack — NOT in the SDK) ─────────
include("${CMAKE_SOURCE_DIR}/cmake/FrameLiftDeps.cmake")

set(FRAMELIFT_GRAPHICS_INCLUDE_DIRS
        "${CMAKE_SOURCE_DIR}/modules/gfx/graphics-core"
        "${CMAKE_SOURCE_DIR}/modules/gfx/opengl"
        "${CMAKE_SOURCE_DIR}/modules/gfx/vulkan"
)
set(FRAMELIFT_HOST_MODULE_INCLUDE_DIRS
        "${CMAKE_SOURCE_DIR}/modules/host/module-runtime"
        "${CMAKE_SOURCE_DIR}/modules/host/settings"
        "${CMAKE_SOURCE_DIR}/modules/host/services"
        "${CMAKE_SOURCE_DIR}/modules/host/controls"
        "${CMAKE_SOURCE_DIR}/modules/host/logging"
        "${CMAKE_SOURCE_DIR}/modules/host/audio"
        "${CMAKE_SOURCE_DIR}/modules/host/playback"
        "${CMAKE_SOURCE_DIR}/modules/host/fonts"
        "${CMAKE_SOURCE_DIR}/modules/host/read-ahead"
        "${CMAKE_SOURCE_DIR}/modules/host/ui"
        "${CMAKE_SOURCE_DIR}/modules/platform/window-qt"
        "${CMAKE_SOURCE_DIR}/modules/platform/dir-watch"
        "${CMAKE_SOURCE_DIR}/modules/platform/win-shell"
)

# ── Shader → SPIR-V build step (glslang, fetched in-tree) ─────────────────────
if (FRAMELIFT_MODULE_GRAPHICS_VULKAN)
    include("${CMAKE_SOURCE_DIR}/cmake/FrameLiftShaders.cmake")
endif ()

# ── Release-build optimizations (FrameLift_apply_release_opts) ─────────────────────
include("${CMAKE_SOURCE_DIR}/cmake/FrameLiftOptimize.cmake")

# ── Version header ───────────────────────────────────────────────────────────
configure_file("${CMAKE_SOURCE_DIR}/src/Version.h.in"
        "${CMAKE_CURRENT_BINARY_DIR}/Version.h")

# ── Embed Icon.png as a C byte array ─────────────────────────────────────────
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/assets/img/Icon.png")
file(READ "${CMAKE_SOURCE_DIR}/assets/img/Icon.png" ICON_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," ICON_C_ARRAY "${ICON_HEX}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/IconData.h"
        "// Auto-generated from assets/img/Icon.png — do not edit\n"
        "inline constexpr unsigned char kIconData[] = {${ICON_C_ARRAY}};\n"
        "inline constexpr unsigned int kIconDataSize = sizeof(kIconData);\n")

# ── Embed Roboto-Regular.ttf as a C byte array (default UI font) ──────────────
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/assets/fonts/Roboto-Regular.ttf")
file(READ "${CMAKE_SOURCE_DIR}/assets/fonts/Roboto-Regular.ttf" FONT_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," FONT_C_ARRAY "${FONT_HEX}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/FontData.h"
        "// Auto-generated from assets/fonts/Roboto-Regular.ttf — do not edit\n"
        "inline constexpr unsigned char kDefaultFontData[] = {${FONT_C_ARRAY}};\n"
        "inline constexpr unsigned int kDefaultFontDataSize = sizeof(kDefaultFontData);\n")

# ── Public plugin SDK ────────────────────────────────────────────────────────
# Defines FrameLiftSdk (headers + Qt), add_framelift_plugin(), and helper sources.
# This same module ships inside
# the SDK package and is loaded by FrameLiftSdkConfig.cmake for standalone builds.
include("${CMAKE_SOURCE_DIR}/cmake/FrameLiftSdk.cmake")

# ── Main executable ───────────────────────────────────────────────────────────
include("${CMAKE_SOURCE_DIR}/cmake/FrameLiftHostSources.cmake")
set(FRAMELIFT_SOURCES ${FRAMELIFT_HOST_SOURCES})

if (WIN32)
    enable_language(RC)
    configure_file("${CMAKE_SOURCE_DIR}/src/FrameLift.rc.in"
            "${CMAKE_CURRENT_BINARY_DIR}/FrameLift.rc" @ONLY)
    list(APPEND FRAMELIFT_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/FrameLift.rc")
endif ()

add_executable(FrameLift ${FRAMELIFT_SOURCES} ${FRAMELIFT_SDK_SOURCES})
set_source_files_properties(
        qml/FrameLift/Controls/Theme.qml
        PROPERTIES QT_RESOURCE_ALIAS Theme.qml QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(qml/FrameLift/Controls/GlassPanel.qml PROPERTIES QT_RESOURCE_ALIAS GlassPanel.qml)
set_source_files_properties(qml/FrameLift/Controls/ActionButton.qml PROPERTIES QT_RESOURCE_ALIAS ActionButton.qml)
set_source_files_properties(qml/FrameLift/Controls/Drawer.qml PROPERTIES QT_RESOURCE_ALIAS Drawer.qml)
qt_add_qml_module(
        FrameLift
        URI FrameLift.Controls
        VERSION 1.0
        RESOURCE_PREFIX "/qt/qml"
        NO_PLUGIN
        QML_FILES
        qml/FrameLift/Controls/Theme.qml
        qml/FrameLift/Controls/GlassPanel.qml
        qml/FrameLift/Controls/ActionButton.qml
        qml/FrameLift/Controls/Drawer.qml)
framelift_apply_builtin_module_definitions(FrameLift)

set_target_properties(FrameLift PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        WIN32_EXECUTABLE TRUE
        # Ship a lowercase artifact name (framelift / framelift.exe) to match the
        # lowercase package DLLs and the packages/ folder.
        OUTPUT_NAME "framelift"
)

# FrameLiftSdk now carries only the public headers; the host links Qt and uses
# stb headers directly, since they no longer cross the plugin boundary.
target_include_directories(FrameLift PRIVATE "${CMAKE_SOURCE_DIR}/src" "${stb_SOURCE_DIR}")
target_include_directories(FrameLift PRIVATE
        ${FRAMELIFT_GRAPHICS_INCLUDE_DIRS}
        ${FRAMELIFT_HOST_MODULE_INCLUDE_DIRS})
target_link_libraries(FrameLift PRIVATE
        FrameLiftSdk OpenGL::GL
        Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Multimedia Qt6::Widgets)

# Vulkan graphics backend (OpenGL→Vulkan migration, #17). volk is the meta-loader
# (compiled here exactly once — imgui only takes volk_headers); VMA is single-source.
# Instance/device/swapchain setup is hand-rolled. No link-time
# libvulkan: volk loads it at runtime.
if (FRAMELIFT_MODULE_GRAPHICS_VULKAN)
    target_link_libraries(FrameLift PRIVATE
            volk Vulkan::Headers GPUOpen::VulkanMemoryAllocator)
endif ()

# Compile the video shaders to embedded SPIR-V headers and make the host depend on
# them (the Vulkan renderer #includes the generated <name>.spv.h from this dir).
if (FRAMELIFT_MODULE_GRAPHICS_VULKAN)
    framelift_compile_shader("${CMAKE_SOURCE_DIR}/modules/gfx/vulkan/shaders/video.vert" kVideoVertSpv FL_VIDEO_VERT_SPV)
    framelift_compile_shader("${CMAKE_SOURCE_DIR}/modules/gfx/vulkan/shaders/video.frag" kVideoFragSpv FL_VIDEO_FRAG_SPV)
    target_sources(FrameLift PRIVATE "${FL_VIDEO_VERT_SPV}" "${FL_VIDEO_FRAG_SPV}")
    target_include_directories(FrameLift PRIVATE "${FRAMELIFT_SHADER_GEN_DIR}")
endif ()

# The FFmpeg + libass playback backend (the only backend; App.cpp constructs
# FFmpegPlayer unconditionally).
target_link_libraries(FrameLift PRIVATE ffmpeg libass)

# Packages load via Qt's QPluginLoader (which uses dlopen on POSIX internally);
# link libdl explicitly so the symbols resolve (no-op libs on Windows).
target_link_libraries(FrameLift PRIVATE ${CMAKE_DL_LIBS})

# winmm: timeBeginPeriod/timeEndPeriod, used to tighten the video frame-pacing timer
# (the waitable-timer / event APIs live in kernel32, linked by default).
if (WIN32)
    target_link_libraries(FrameLift PRIVATE winmm)
endif ()

# Windows shell integration (taskbar progress + toast notifications): COM
# (ole32), the shell shortcut/AUMID APIs (shell32), and the WinRT activation
# runtime for the Windows.UI.Notifications ABI (runtimeobject).
if (FRAMELIFT_MODULE_WIN_SHELL)
    target_link_libraries(FrameLift PRIVATE ole32 shell32 runtimeobject)
endif ()

if (MINGW)
    target_link_options(FrameLift PRIVATE -static-libgcc -static-libstdc++ -static)
endif ()

FrameLift_apply_release_opts(FrameLift)

# Create packages/ subdirectory before the first package DLL is built.
add_custom_command(TARGET FrameLift PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:FrameLift>/packages"
)

# Discover plugin package source folders automatically. Each plugin CMakeLists
# registers its target through add_framelift_plugin(). Skipped entirely during the
# Qt migration (FRAMELIFT_BUILD_PLUGINS=OFF) — the plugins are still ImGui-based and
# Phase 1 ships host-only video playback. They will be reimplemented (QPluginLoader)
# in a later phase.
if (FRAMELIFT_BUILD_PLUGINS)
    set_property(GLOBAL PROPERTY FRAMELIFT_PLUGIN_TARGETS)
    file(GLOB FRAMELIFT_PLUGIN_DIRS CONFIGURE_DEPENDS LIST_DIRECTORIES TRUE
            RELATIVE "${CMAKE_SOURCE_DIR}/plugins"
            "${CMAKE_SOURCE_DIR}/plugins/*")
    list(SORT FRAMELIFT_PLUGIN_DIRS)
    foreach (_plugin_dir_name IN LISTS FRAMELIFT_PLUGIN_DIRS)
        if (NOT EXISTS "${CMAKE_SOURCE_DIR}/plugins/${_plugin_dir_name}/CMakeLists.txt")
            continue()
        endif ()
        add_subdirectory("plugins/${_plugin_dir_name}")
    endforeach ()

    get_property(FRAMELIFT_PLUGIN_TARGETS GLOBAL PROPERTY FRAMELIFT_PLUGIN_TARGETS)
    if (FRAMELIFT_PLUGIN_TARGETS)
        add_dependencies(FrameLift ${FRAMELIFT_PLUGIN_TARGETS})
    endif ()

    # Apply the same Release optimizations to each shipped plugin DLL (LTO is
    # per-binary, so the host's doesn't reach into the plugins).
    foreach (_plugin IN LISTS FRAMELIFT_PLUGIN_TARGETS)
        get_target_property(_plugin_disabled ${_plugin} FRAMELIFT_PLUGIN_DISABLED)
        if (NOT _plugin_disabled)
            framelift_apply_builtin_module_definitions(${_plugin})
            target_include_directories(${_plugin} PRIVATE
                    ${FRAMELIFT_GRAPHICS_INCLUDE_DIRS}
                    ${FRAMELIFT_HOST_MODULE_INCLUDE_DIRS})
            FrameLift_apply_release_opts(${_plugin})
        endif ()
    endforeach ()
endif ()

# ── Runtime DLLs ──────────────────────────────────────────────────────────────
# On Linux the shared objects are resolved from the system via ldconfig. On
# Windows Qt6, FFmpeg, and libass all come from vcpkg; their runtime DLLs
# (Qt6*.dll, avcodec-*.dll, ass-*.dll) and transitive deps (freetype/fontconfig/
# harfbuzz/fribidi/…) are deployed next to framelift.exe automatically by vcpkg's
# app-local deployment (VCPKG_APPLOCAL_DEPS) — no manual copy needed. Note: Qt's
# QML/plugin runtime (platform plugins, QML modules) may additionally need
# windeployqt; revisit when packaging.

# ── Unit tests (native, IDE-visible) ─────────────────────────────────────────
# Compiles only platform-independent units; safe to configure alongside the app.
# CI's Windows build passes -DFRAMELIFT_BUILD_TESTS=OFF (native test exes can't run
# under the cross-compile); the dedicated CI `test` job builds tests/ standalone.
if (FRAMELIFT_BUILD_TESTS)
    add_subdirectory(tests)
endif ()

# ── Plugin SDK packaging ──────────────────────────────────────────────────────
# Stage a standalone, dependency-free SDK that external authors build plugins
# against. Inert during a normal build; materialised via `cmake --install ...
# --component sdk` + `cpack` (see .github/workflows/ci.yml). The package version tracks the
# ABI version so find_package(FrameLiftSdk) can gate on ExactVersion.
# The version is parsed from the SDK header — single source of truth.
set(_FrameLift_abi_header "${CMAKE_SOURCE_DIR}/sdk/include/framelift/ModuleABI.h")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_FrameLift_abi_header}")
# Same whitespace-tolerant regex as FrameLiftPackageMetadata.cmake's standalone-SDK
# fallback, so either parse path accepts spaces or tabs after the macro name.
file(STRINGS "${_FrameLift_abi_header}" _FrameLift_abi_line
        REGEX "^#define[ \t]+FRAMELIFT_ABI_VERSION[ \t]+[0-9]+")
if (NOT _FrameLift_abi_line MATCHES "FRAMELIFT_ABI_VERSION[ \t]+([0-9]+)")
    message(FATAL_ERROR "Could not parse FRAMELIFT_ABI_VERSION from ${_FrameLift_abi_header}")
endif ()
set(FRAMELIFT_SDK_ABI_VERSION "${CMAKE_MATCH_1}")
unset(_FrameLift_abi_line)
unset(_FrameLift_abi_header)

include(CMakePackageConfigHelpers)
configure_package_config_file(
        "${CMAKE_SOURCE_DIR}/cmake/FrameLiftSdkConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/sdk-pkg/FrameLiftSdkConfig.cmake"
        INSTALL_DESTINATION "cmake")
write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/sdk-pkg/FrameLiftSdkConfigVersion.cmake"
        VERSION "${FRAMELIFT_SDK_ABI_VERSION}"
        COMPATIBILITY ExactVersion)

install(DIRECTORY "${CMAKE_SOURCE_DIR}/sdk/include/"
        DESTINATION include COMPONENT sdk)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/sdk/src/"
        DESTINATION src COMPONENT sdk)
install(FILES
        "${CMAKE_SOURCE_DIR}/cmake/FrameLiftSdk.cmake"
        "${CMAKE_SOURCE_DIR}/cmake/FrameLiftPackageMetadata.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/sdk-pkg/FrameLiftSdkConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/sdk-pkg/FrameLiftSdkConfigVersion.cmake"
        DESTINATION cmake COMPONENT sdk)
install(FILES "${CMAKE_SOURCE_DIR}/sdk/package-template/CMakeLists.txt"
        DESTINATION . COMPONENT sdk)
install(FILES "${CMAKE_SOURCE_DIR}/sdk/README.md" "${CMAKE_SOURCE_DIR}/LICENSE"
        DESTINATION . COMPONENT sdk)

# ── CPack: one ZIP for the sdk component → FrameLift-sdk-<ver>.zip ──────────────────
set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_NAME "FrameLift-sdk")
set(CPACK_PACKAGE_VERSION "${FRAMELIFT_VERSION}")
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
set(CPACK_COMPONENTS_ALL sdk)
set(CPACK_ARCHIVE_SDK_FILE_NAME "FrameLift-sdk-${FRAMELIFT_VERSION}")
include(CPack)
