cmake_minimum_required(VERSION 4.2.0)

set(FRAMELIFT_VERSION "0.0.0" CACHE STRING "Numeric version passed to CMake project() — must be X.Y.Z")
project(FrameLift VERSION ${FRAMELIFT_VERSION} LANGUAGES CXX)

# The auto-update plugin links winhttp, so it's Windows-only. Forced OFF (and
# hidden) on every other platform — e.g. the native-Linux test/CI builds.
include(CMakeDependentOption)
cmake_dependent_option(FRAMELIFT_ENABLE_UPDATER
        "Enable the auto-update plugin (requires winhttp)" ON
        "WIN32" OFF)

# Build the native unit-test suite (tests/) as part of this project so it shows up
# in IDEs. Disabled for the Windows app/CI build, which can't run native test exes.
option(FRAMELIFT_BUILD_TESTS "Build the unit tests (tests/)" ON)

# Display string shown in the UI; defaults to the numeric version for local/release builds.
if (NOT DEFINED FRAMELIFT_VERSION_DISPLAY OR FRAMELIFT_VERSION_DISPLAY STREQUAL "")
    set(FRAMELIFT_VERSION_DISPLAY "${FRAMELIFT_VERSION}")
endif ()

include(FetchContent)

# ── Platform libraries (SDL3 + FFmpeg + libass) ───────────────────────────────
# Defines the SDL3::SDL3, `ffmpeg`, and `libass` targets: Linux uses system dev
# packages (libsdl3-dev / libav*-dev / libass-dev); Windows resolves them all from
# the vcpkg manifest (vcpkg.json). Resolved before the host third-party deps
# because the imgui static lib (FrameLiftDeps.cmake) links SDL3::SDL3.
include("${CMAKE_SOURCE_DIR}/cmake/FrameLiftPlatformLibs.cmake")

# ── OpenGL (opengl32 on Windows, libGL on Linux) ──────────────────────────────
find_package(OpenGL REQUIRED)

# ── Host third-party dependencies (imgui/spdlog/stb/json — NOT in the SDK) ────
include("${CMAKE_SOURCE_DIR}/cmake/FrameLiftDeps.cmake")

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
# Defines the dependency-clean FrameLiftSdk target (headers only), the
# add_FrameLift_plugin() macro, and FRAMELIFT_SDK_SOURCES. This same module ships inside
# the SDK package and is loaded by FrameLiftSdkConfig.cmake for standalone builds.
include("${CMAKE_SOURCE_DIR}/cmake/FrameLiftSdk.cmake")

# ── Main executable ───────────────────────────────────────────────────────────
file(GLOB_RECURSE FRAMELIFT_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_SOURCE_DIR}/src/*.h"
)

if (WIN32)
    enable_language(RC)
    configure_file("${CMAKE_SOURCE_DIR}/src/FrameLift.rc.in"
            "${CMAKE_CURRENT_BINARY_DIR}/FrameLift.rc" @ONLY)
    list(APPEND FRAMELIFT_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/FrameLift.rc")
endif ()

add_executable(FrameLift ${FRAMELIFT_SOURCES} ${FRAMELIFT_SDK_SOURCES})

set_target_properties(FrameLift PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        WIN32_EXECUTABLE TRUE
)

# FrameLiftSdk now carries only the public headers; the host links the third-party
# libraries (imgui/spdlog/json) and stb headers directly, since they no longer
# cross the plugin boundary.
target_include_directories(FrameLift PRIVATE "${CMAKE_SOURCE_DIR}/src" "${stb_SOURCE_DIR}")
target_link_libraries(FrameLift PRIVATE
        FrameLiftSdk SDL3::SDL3 OpenGL::GL
        imgui spdlog::spdlog nlohmann_json::nlohmann_json)

# Vulkan graphics backend (OpenGL→Vulkan migration, #17). volk is the meta-loader
# (compiled here exactly once — imgui only takes volk_headers); VMA + vk-bootstrap
# are header/single-source. No link-time libvulkan: volk loads it at runtime.
target_link_libraries(FrameLift PRIVATE
        volk Vulkan::Headers GPUOpen::VulkanMemoryAllocator vk-bootstrap::vk-bootstrap)

# The FFmpeg + libass playback backend (the only backend; App.cpp constructs
# FFmpegPlayer unconditionally).
target_link_libraries(FrameLift PRIVATE ffmpeg libass)

# The plugin loader uses dlopen/dlsym on POSIX (no-op libs on Windows).
target_link_libraries(FrameLift PRIVATE ${CMAKE_DL_LIBS})

# winmm: timeBeginPeriod/timeEndPeriod, used to tighten the video frame-pacing timer
# (the waitable-timer / event APIs live in kernel32, linked by default).
if (WIN32)
    target_link_libraries(FrameLift PRIVATE winmm)
endif ()

if (FRAMELIFT_ENABLE_UPDATER)
    target_compile_definitions(FrameLift PRIVATE FRAMELIFT_ENABLE_UPDATER)
    target_link_libraries(FrameLift PRIVATE winhttp)
endif ()

if (MINGW)
    target_link_options(FrameLift PRIVATE -static-libgcc -static-libstdc++ -static)
endif ()

FrameLift_apply_release_opts(FrameLift)

# Create Plugins/ subdirectory before the first DLL is built.
add_custom_command(TARGET FrameLift PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:FrameLift>/Plugins"
)

# ── Plugin DLL targets ────────────────────────────────────────────────────────
add_subdirectory(plugins/Overlay)
add_subdirectory(plugins/Playlist)
add_subdirectory(plugins/History)
add_subdirectory(plugins/SettingsMenu)
add_subdirectory(plugins/DebugOverlay)
add_subdirectory(plugins/Benchmark)
add_subdirectory(plugins/RemoteStream)
if (FRAMELIFT_ENABLE_UPDATER)
    add_subdirectory(plugins/Updater)
endif ()

# ── Make plugin DLLs build whenever FrameLift builds ───────────────────────────────
set(FRAMELIFT_PLUGIN_TARGETS
        Overlay
        Playlist
        History
        SettingsMenu
        DebugOverlay
        Benchmark
        RemoteStream
)
if (FRAMELIFT_ENABLE_UPDATER)
    list(APPEND FRAMELIFT_PLUGIN_TARGETS Updater)
endif ()
add_dependencies(FrameLift ${FRAMELIFT_PLUGIN_TARGETS})

# Apply the same Release optimizations to each shipped plugin DLL (LTO is
# per-binary, so the host's doesn't reach into the plugins).
foreach (_plugin IN LISTS FRAMELIFT_PLUGIN_TARGETS)
    FrameLift_apply_release_opts(${_plugin})
endforeach ()

# ── Runtime DLLs ──────────────────────────────────────────────────────────────
# On Linux the shared objects are resolved from the system via ldconfig. On
# Windows SDL3, FFmpeg, and libass all come from vcpkg; their runtime DLLs
# (SDL3.dll, avcodec-*.dll, ass-*.dll) and transitive deps (freetype/fontconfig/
# harfbuzz/fribidi/…) are deployed next to FrameLift.exe automatically by vcpkg's
# app-local deployment (VCPKG_APPLOCAL_DEPS) — no manual copy needed.

# ── SDK example (bitrot guard) ────────────────────────────────────────────────
# Build the bundled example plugin in-tree so SDK breakage is caught by the
# normal build. It is not added to the host's enabled-plugins list.
option(FRAMELIFT_BUILD_SDK_EXAMPLE "Build the bundled HelloPlugin example in-tree" ON)
if (FRAMELIFT_BUILD_SDK_EXAMPLE)
    add_subdirectory(sdk/examples/HelloPlugin)
    # Keep the example out of the shipped Plugins/ dir (it's only a bitrot guard).
    # Override both RUNTIME (.dll) and LIBRARY (.so) so it's redirected on every platform.
    set_target_properties(HelloPlugin PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/sdk-example"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/sdk-example")
    add_dependencies(FrameLift HelloPlugin)
endif ()

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
# ABI version so find_package(FrameLiftSdk) can gate on SameMajorVersion.
# ABI version is parsed from the SDK header — single source of truth.
set(_FrameLift_abi_header "${CMAKE_SOURCE_DIR}/sdk/include/framelift/PluginABI.h")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_FrameLift_abi_header}")
foreach (_part MAJOR MINOR PATCH)
    file(STRINGS "${_FrameLift_abi_header}" _FrameLift_abi_line
            REGEX "^#define FRAMELIFT_PLUGIN_ABI_${_part} +[0-9]+")
    if (NOT _FrameLift_abi_line MATCHES "FRAMELIFT_PLUGIN_ABI_${_part} +([0-9]+)")
        message(FATAL_ERROR "Could not parse FRAMELIFT_PLUGIN_ABI_${_part} from ${_FrameLift_abi_header}")
    endif ()
    set(FRAMELIFT_SDK_ABI_${_part} "${CMAKE_MATCH_1}")
endforeach ()
unset(_FrameLift_abi_line)
unset(_FrameLift_abi_header)

include(CMakePackageConfigHelpers)
configure_package_config_file(
        "${CMAKE_SOURCE_DIR}/cmake/FrameLiftSdkConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/sdk-pkg/FrameLiftSdkConfig.cmake"
        INSTALL_DESTINATION "cmake")
write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/sdk-pkg/FrameLiftSdkConfigVersion.cmake"
        VERSION "${FRAMELIFT_SDK_ABI_MAJOR}.${FRAMELIFT_SDK_ABI_MINOR}.${FRAMELIFT_SDK_ABI_PATCH}"
        COMPATIBILITY SameMajorVersion)

install(DIRECTORY "${CMAKE_SOURCE_DIR}/sdk/include/"
        DESTINATION include COMPONENT sdk)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/sdk/src/"
        DESTINATION src COMPONENT sdk)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/sdk/examples/"
        DESTINATION examples COMPONENT sdk)
install(FILES
        "${CMAKE_SOURCE_DIR}/cmake/FrameLiftSdk.cmake"
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
