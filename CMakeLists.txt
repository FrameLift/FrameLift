cmake_minimum_required(VERSION 4.2.0)

set(FRAMELIFT_VERSION "0.0.0" CACHE STRING "Numeric version passed to CMake project() — must be X.Y.Z")
project(FrameLift VERSION ${FRAMELIFT_VERSION} LANGUAGES C CXX)

# Build the native unit-test suite (tests/) as part of this project so it shows up
# in IDEs. Disabled for the Windows app/CI build, which can't run native test exes.
option(FRAMELIFT_BUILD_TESTS "Build the unit tests (tests/)" OFF)

# Build runtime plugin DLLs/SOs in plugins/. Plugins load through QPluginLoader
# and embed their Qt Quick surfaces as QML modules.
option(FRAMELIFT_BUILD_PLUGINS "Build the runtime plugin DLLs/SOs (plugins/)" ON)

# Build-only hook for CTest launch smokes. Off in normal app/release builds so
# test-only environment controls do not exist in the shipped executable.
option(FRAMELIFT_BUILD_LAUNCH_TESTS "Build CTest launch smoke hooks for the app" OFF)

# Extra launch smoke on a real Vulkan device with validation layers forced on. Needs an
# X display (CI: Xvfb), a Vulkan ICD (CI: lavapipe from mesa-vulkan-drivers) and the
# Khronos validation layer installed — hence opt-in on top of the launch smokes.
option(FRAMELIFT_LAUNCH_VULKAN_SMOKE "Add the Vulkan+validation launch smoke test" OFF)

# Display string shown in the UI; defaults to the numeric version for local/release builds.
if (NOT DEFINED FRAMELIFT_VERSION_DISPLAY OR FRAMELIFT_VERSION_DISPLAY STREQUAL "")
    set(FRAMELIFT_VERSION_DISPLAY "${FRAMELIFT_VERSION}")
endif ()

enable_testing()

include(FetchContent)

# Built-in host modules are JSON-authored and drive compile-time ownership.
include("${CMAKE_SOURCE_DIR}/cmake/FrameLiftBuiltinModules.cmake")
framelift_load_builtin_modules("${CMAKE_SOURCE_DIR}/modules")
framelift_report_builtin_modules()

# ── Platform media libraries (external FFmpeg + libass) ───────────────────────
# Defines the `ffmpeg` and `libass` targets: Linux uses system dev packages
# (libav*-dev / libass-dev); Windows resolves them from the vcpkg manifest
# (vcpkg.json). Qt is resolved separately from a prebuilt install in CI. This is
# FrameLift's media playback stack: demux, decode,
# filtering, subtitles, and hardware decode all go through this external FFmpeg
# target, not through Qt Multimedia's media playback backend.
include("${CMAKE_SOURCE_DIR}/cmake/FrameLiftPlatformLibs.cmake")

# ── OpenGL (opengl32 on Windows, libGL on Linux) ──────────────────────────────
find_package(OpenGL REQUIRED)

# ── Host third-party dependencies (Vulkan stack — NOT in the SDK) ─────────────
# Run before finding Qt so Qt's WrapVulkanHeaders helper can see FrameLift's
# fetched Vulkan-Headers when the system does not provide Vulkan_INCLUDE_DIR.
include("${CMAKE_SOURCE_DIR}/cmake/FrameLiftDeps.cmake")

# ── Qt6 (window / events / raw PCM audio / UI) ────────────────────────────────
# Gui+Quick drive the QQuickWindow and scene graph (video is a QSGRenderNode);
# Multimedia is linked only for QAudioSink/QMediaDevices raw PCM output. Do not
# use Qt media playback classes here: compressed media stays on FFmpegPlayer and
# the external `ffmpeg` target above. Widgets provides the native QFileDialog
# open-file picker.
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Multimedia Widgets)

# Guard: no Qt Multimedia playback APIs (compressed media stays on external FFmpeg).
include("${CMAKE_SOURCE_DIR}/cmake/FrameLiftMediaAudit.cmake")
framelift_assert_no_qt_media_playback()

# Host/gfx internal include dirs (shared with the test build).
include("${CMAKE_SOURCE_DIR}/cmake/FrameLiftHostIncludes.cmake")

# ── Shader → SPIR-V build step (glslang, fetched in-tree) ─────────────────────
if (FRAMELIFT_MODULE_GRAPHICS_VULKAN)
    include("${CMAKE_SOURCE_DIR}/cmake/FrameLiftShaders.cmake")
endif ()

# ── Release-build optimizations (framelift_apply_release_opts) ─────────────────────
include("${CMAKE_SOURCE_DIR}/cmake/FrameLiftOptimize.cmake")

# ── Version header ───────────────────────────────────────────────────────────
configure_file("${CMAKE_SOURCE_DIR}/src/Version.h.in"
        "${CMAKE_CURRENT_BINARY_DIR}/Version.h")

# ── Public plugin SDK ────────────────────────────────────────────────────────
# Defines FrameLiftSdk (headers + Qt), add_framelift_plugin(), and helper sources.
# This same module ships inside
# the SDK package and is loaded by FrameLiftSdkConfig.cmake for standalone builds.
include("${CMAKE_SOURCE_DIR}/cmake/FrameLiftSdk.cmake")

# ── Main executable ───────────────────────────────────────────────────────────
# FrameLiftHostSources defines one OBJECT library per built-in module (linked below);
# the exe itself compiles only the app entry point and the header-only modules.
include("${CMAKE_SOURCE_DIR}/cmake/FrameLiftHostSources.cmake")
set(FRAMELIFT_SOURCES ${FRAMELIFT_HOST_CORE_SOURCES} ${FRAMELIFT_HOST_HEADER_ONLY_SOURCES})

if (WIN32)
    enable_language(RC)
    configure_file("${CMAKE_SOURCE_DIR}/packaging/windows/framelift.rc.in"
            "${CMAKE_CURRENT_BINARY_DIR}/FrameLift.rc" @ONLY)
    list(APPEND FRAMELIFT_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/FrameLift.rc")
endif ()

add_executable(FrameLift ${FRAMELIFT_SOURCES} ${FRAMELIFT_SDK_SOURCES})
# AUTOMOC for the app entry point and required by qt_add_qml_module's type registration
# (the module OBJECT libraries enable AUTOMOC on their own targets).
set_target_properties(FrameLift PROPERTIES AUTOMOC ON)
set(_framelift_control_qml
        qml/FrameLift/Controls/FLTheme.qml
        qml/FrameLift/Controls/FLSettingsUiState.qml
        qml/FrameLift/Controls/FLGlassPanel.qml
        qml/FrameLift/Controls/FLActionButton.qml
        qml/FrameLift/Controls/FLDrawer.qml
        qml/FrameLift/Controls/FLConfirmDialog.qml
        qml/FrameLift/Controls/FLSettingsGroup.qml
        qml/FrameLift/Controls/FLSettingRow.qml
        qml/FrameLift/Controls/FLSwitch.qml
        qml/FrameLift/Controls/FLTextField.qml
        qml/FrameLift/Controls/FLSpinBox.qml
        qml/FrameLift/Controls/FLComboBox.qml
        qml/FrameLift/Controls/FLSlider.qml
        qml/FrameLift/Controls/FLKeyCapButton.qml
        qml/FrameLift/Controls/FLNavigableListView.qml
        qml/FrameLift/Controls/FLListRow.qml)
# Resource alias is the basename, so each lands at /qt/qml/FrameLift/Controls/<File>.qml.
foreach (_qml IN LISTS _framelift_control_qml)
    get_filename_component(_qml_alias "${_qml}" NAME)
    set_source_files_properties("${_qml}" PROPERTIES QT_RESOURCE_ALIAS "${_qml_alias}")
endforeach ()
# FLTheme and FLSettingsUiState are QML singletons (pragma Singleton) — flag them for the registrar.
set_source_files_properties(qml/FrameLift/Controls/FLTheme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(qml/FrameLift/Controls/FLSettingsUiState.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(
        FrameLift
        URI FrameLift.Controls
        VERSION 1.0
        RESOURCE_PREFIX "/qt/qml"
        NO_PLUGIN
        QML_FILES ${_framelift_control_qml})
set_source_files_properties("${CMAKE_SOURCE_DIR}/assets/img/icon.svg" PROPERTIES QT_RESOURCE_ALIAS icon.svg)
qt_add_resources(
        FrameLift framelift_assets
        PREFIX "/framelift/assets"
        FILES "${CMAKE_SOURCE_DIR}/assets/img/icon.svg")
framelift_apply_builtin_module_definitions(FrameLift)
if (FRAMELIFT_BUILD_LAUNCH_TESTS)
    target_compile_definitions(FrameLift PRIVATE FRAMELIFT_BUILD_LAUNCH_TESTS=1)
else ()
    target_compile_definitions(FrameLift PRIVATE FRAMELIFT_BUILD_LAUNCH_TESTS=0)
endif ()

set_target_properties(FrameLift PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        WIN32_EXECUTABLE TRUE
        # Ship a lowercase artifact name (framelift / framelift.exe) to match the
        # lowercase plugin DLLs/SOs and the plugins/ folder.
        OUTPUT_NAME "framelift"
)

# FrameLiftSdk carries only the public headers; the host links Qt and keeps
# implementation dependencies private.
target_include_directories(FrameLift PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_include_directories(FrameLift PRIVATE
        ${FRAMELIFT_GRAPHICS_INCLUDE_DIRS}
        ${FRAMELIFT_HOST_MODULE_INCLUDE_DIRS})
target_link_libraries(FrameLift PRIVATE
        FrameLiftSdk OpenGL::GL
        Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Multimedia Qt6::Widgets)

# Built-in module OBJECT libraries (defined in FrameLiftHostSources); the exe links the
# objects + their compile usage requirements.
get_property(_framelift_host_modules GLOBAL PROPERTY FRAMELIFT_HOST_MODULE_TARGETS)
target_link_libraries(FrameLift PRIVATE ${_framelift_host_modules})

# Vulkan graphics backend. FrameLift owns the instance/device setup and links the
# official Vulkan loader; VMA is header-only. (The module object libs compile against
# the Vulkan headers; the exe links the loader.)
if (FRAMELIFT_MODULE_GRAPHICS_VULKAN)
    target_link_libraries(FrameLift PRIVATE
            Vulkan::Vulkan Vulkan::Headers GPUOpen::VulkanMemoryAllocator)
endif ()

# The external FFmpeg + libass playback backend. This is the only compressed
# media path: App.cpp constructs FFmpegPlayer unconditionally. Qt Multimedia may
# log its own backend discovery (including qt.multimedia.ffmpeg) because we link
# it for QAudioSink, but FrameLift does not route media files through Qt.
target_link_libraries(FrameLift PRIVATE ffmpeg libass)

# Plugins load via Qt's QPluginLoader (which uses dlopen on POSIX internally);
# link libdl explicitly so the symbols resolve (no-op libs on Windows).
target_link_libraries(FrameLift PRIVATE ${CMAKE_DL_LIBS})

if (MINGW)
    target_link_options(FrameLift PRIVATE -static-libgcc -static-libstdc++ -static)
endif ()

framelift_apply_release_opts(FrameLift)

# Create plugins/ subdirectory before the first plugin DLL/SO is built.
add_custom_command(TARGET FrameLift PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:FrameLift>/plugins"
)

# Discover plugin source folders automatically. Each plugin CMakeLists
# registers its target through add_framelift_plugin().
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

    # Apply the same Release optimizations to each shipped plugin DLL/SO (LTO is
    # per-binary, so the host's doesn't reach into the plugins). Plugins see only the
    # public SDK and their own dir — never host/gfx internals (a plugin that needs a
    # host source, e.g. SettingsMenu, opts into those include dirs itself).
    foreach (_plugin IN LISTS FRAMELIFT_PLUGIN_TARGETS)
        get_target_property(_plugin_disabled ${_plugin} FRAMELIFT_PLUGIN_DISABLED)
        if (NOT _plugin_disabled)
            framelift_apply_release_opts(${_plugin})
        endif ()
    endforeach ()
endif ()

# ── Runtime DLLs ──────────────────────────────────────────────────────────────
# On Linux the shared objects are resolved from the system via ldconfig. On
# Windows, FFmpeg/libass come from vcpkg and Qt comes from a prebuilt Qt install;
# the CI package job stages the Qt runtime with windeployqt, then copies the
# FFmpeg/libass runtime DLLs from vcpkg next to framelift.exe.

# ── Linux desktop integration (icon in the GNOME/KDE shell) ───────────────────
# The shell resolves the dock/overview icon by matching the window app_id /
# WM_CLASS ("framelift", pinned via QApplication::setDesktopFileName) against an
# installed .desktop file, whose Icon= key is looked up in the hicolor icon theme.
# Install both so a packaged build shows the real icon instead of a placeholder.
# (Running straight from the build tree won't show it — the files must live under
# an XDG data dir; `cmake --install` or the packaged tarball provides that.)
if (UNIX AND NOT APPLE)
    install(FILES "${CMAKE_SOURCE_DIR}/packaging/linux/framelift.desktop"
            DESTINATION share/applications COMPONENT runtime)
    install(FILES "${CMAKE_SOURCE_DIR}/assets/img/icon.svg"
            DESTINATION share/icons/hicolor/scalable/apps
            RENAME framelift.svg COMPONENT runtime)
endif ()

# ── Unit tests (native, IDE-visible) ─────────────────────────────────────────
# Compiles only platform-independent units; safe to configure alongside the app.
# CI's Windows build passes -DFRAMELIFT_BUILD_TESTS=OFF (native test exes can't run
# under the cross-compile); the dedicated CI `test` job builds tests/ standalone.
if (FRAMELIFT_BUILD_TESTS)
    add_subdirectory(tests)
endif ()

# ── Runtime launch smoke tests ────────────────────────────────────────────────
# Exercise the real executable plus runtime plugins/QML without joining the
# standalone pure-logic tests/ project. Linux CI can run this headlessly through
# Qt's offscreen platform and software scene-graph backend.
if (UNIX AND NOT APPLE AND FRAMELIFT_BUILD_PLUGINS AND FRAMELIFT_BUILD_LAUNCH_TESTS)
    set(_framelift_launch_env
            "QT_QPA_PLATFORM=offscreen"
            "QT_QUICK_BACKEND=software"
            "FL_BACKEND=gl"
            "FL_ACCEL_MODE=off"
            "FL_LOG_LEVEL=debug"
            "FL_TEST_EXIT_AFTER_MS=1500"
            "XDG_CONFIG_HOME=${CMAKE_BINARY_DIR}/test-config/framelift_launch_opengl_offscreen")
    set(_framelift_launch_fail_regex
            "TypeError"
            "ReferenceError"
            "QML .* root object is not"
            "QML .* skipped"
            "FrameLift startup failed"
            "Segmentation fault"
            "core dumped"
            "Aborted"
            "Assertion"
            "AddressSanitizer"
            "UndefinedBehaviorSanitizer")
    add_test(NAME framelift_launch_opengl_offscreen
            COMMAND $<TARGET_FILE:FrameLift>)
    set_tests_properties(framelift_launch_opengl_offscreen PROPERTIES
            WORKING_DIRECTORY "$<TARGET_FILE_DIR:FrameLift>"
            LABELS launch
            TIMEOUT 20
            ENVIRONMENT "${_framelift_launch_env}"
            FAIL_REGULAR_EXPRESSION "${_framelift_launch_fail_regex}")

    # Same launch on a real Vulkan device with the Khronos validation layer forced on,
    # playing a short software-decoded clip when ${CMAKE_BINARY_DIR}/test-media/smoke.mp4
    # exists (CI generates it; without it the smoke still covers init/teardown). Any
    # message from the validation messenger fails the test, and the PASS regex proves a
    # Vulkan device was actually created (no silent fall-back to OpenGL). Zero-copy
    # decode is not covered: lavapipe has no video-decode queues.
    if (FRAMELIFT_LAUNCH_VULKAN_SMOKE)
        set(_framelift_vulkan_launch_env
                "QT_QPA_PLATFORM=xcb"
                "FL_BACKEND=vulkan"
                "FL_ACCEL_MODE=off"
                "FL_LOG_LEVEL=debug"
                "FL_VULKAN_VALIDATION=1"
                "FL_TEST_EXIT_AFTER_MS=6000"
                "XDG_CONFIG_HOME=${CMAKE_BINARY_DIR}/test-config/framelift_launch_vulkan")
        add_test(NAME framelift_launch_vulkan_validation
                COMMAND $<TARGET_FILE:FrameLift> "${CMAKE_BINARY_DIR}/test-media/smoke.mp4")
        set_tests_properties(framelift_launch_vulkan_validation PROPERTIES
                WORKING_DIRECTORY "$<TARGET_FILE_DIR:FrameLift>"
                LABELS launch
                TIMEOUT 60
                ENVIRONMENT "${_framelift_vulkan_launch_env}"
                PASS_REGULAR_EXPRESSION "device API"
                FAIL_REGULAR_EXPRESSION "${_framelift_launch_fail_regex};Vulkan validation")
    endif ()
endif ()

# ── Plugin SDK packaging ──────────────────────────────────────────────────────
include("${CMAKE_SOURCE_DIR}/cmake/FrameLiftSdkPackage.cmake")
framelift_configure_sdk_package()
