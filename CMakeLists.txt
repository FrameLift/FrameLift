cmake_minimum_required(VERSION 4.2.0)

set(FRAMELIFT_VERSION "0.0.0" CACHE STRING "Numeric version passed to CMake project() — must be X.Y.Z")
project(FrameLift VERSION ${FRAMELIFT_VERSION} LANGUAGES C CXX)

# FrameLift does not use C++ modules. Disabling CMake's compiler dependency scan
# avoids Ninja's dyndep path, which currently asserts in Ninja 1.13.x on this
# project while processing the many Qt-generated translation units.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

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
framelift_generate_builtin_features_header("${CMAKE_CURRENT_BINARY_DIR}/FrameLiftBuiltinFeatures.h")

# The shared AI runtime deliberately consumes a packaged llama.cpp installation
# (vcpkg `llama-cpp[tools]` on Windows, distro packages on Linux). Lean builds
# remove the dependency, host sources, and AI plugins together.
if (FRAMELIFT_MODULE_AI)
    find_package(llama CONFIG QUIET)
    if (NOT llama_FOUND)
        # Debian/Ubuntu (libllama-dev) install llama.cpp under a private prefix
        # (include/llama, <libdir>/llama) without exporting the CMake package.
        find_path(FRAMELIFT_LLAMA_INCLUDE_DIR NAMES llama.h PATH_SUFFIXES llama)
        find_library(FRAMELIFT_LLAMA_LIBRARY NAMES llama PATH_SUFFIXES llama)
        # The AI module calls ggml_log_set directly; the distro packages keep it in
        # libggml-base (libggml-dev), which linking libllama.so does not re-export.
        find_library(FRAMELIFT_GGML_LIBRARY NAMES ggml-base ggml)
        if (FRAMELIFT_LLAMA_INCLUDE_DIR AND FRAMELIFT_LLAMA_LIBRARY AND FRAMELIFT_GGML_LIBRARY)
            add_library(llama SHARED IMPORTED)
            set_target_properties(llama PROPERTIES
                    IMPORTED_LOCATION "${FRAMELIFT_LLAMA_LIBRARY}"
                    INTERFACE_INCLUDE_DIRECTORIES "${FRAMELIFT_LLAMA_INCLUDE_DIR}"
                    INTERFACE_LINK_LIBRARIES "${FRAMELIFT_GGML_LIBRARY}")
            set(llama_FOUND TRUE)
        endif ()
    endif ()
    find_library(FRAMELIFT_MTMD_LIBRARY NAMES mtmd PATH_SUFFIXES llama)
    find_path(FRAMELIFT_MTMD_INCLUDE_DIR NAMES mtmd.h PATH_SUFFIXES llama)
    if (NOT llama_FOUND OR NOT FRAMELIFT_MTMD_LIBRARY OR NOT FRAMELIFT_MTMD_INCLUDE_DIR)
        message(FATAL_ERROR
                "The AI module requires llama.cpp and libmtmd development files. Install them from "
                "your package manager (Debian/Ubuntu: libllama-dev + libggml-dev; Arch: llama.cpp; "
                "Windows: vcpkg llama-cpp[tools]) or point CMAKE_PREFIX_PATH at a llama.cpp install, "
                "or configure a lean build with -DFRAMELIFT_MODULE_AI=OFF.")
    endif ()
endif ()

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
# Sql backs the host IMediaStore service (modules/host/services/MediaStoreImpl) over
# the bundled QSQLITE driver; plugins never link a database library themselves.
find_package(Qt6 6.9 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Multimedia Widgets Sql)
if (FRAMELIFT_MODULE_GRAPHICS_VULKAN)
    find_package(Qt6 6.9 REQUIRED COMPONENTS GuiPrivate)
endif ()
if (FRAMELIFT_MODULE_AI)
    find_package(Qt6 REQUIRED COMPONENTS Network)
endif ()

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
        qml/FrameLift/Controls/FLWindowChromeBar.qml
        qml/FrameLift/Controls/FLTitleBar.qml
        qml/FrameLift/Controls/FLWindow.qml
        qml/FrameLift/Controls/FLGlassPanel.qml
        qml/FrameLift/Controls/FLActionButton.qml
        qml/FrameLift/Controls/FLDrawer.qml
        qml/FrameLift/Controls/FLPanelAction.qml
        qml/FrameLift/Controls/FLPanelHeader.qml
        qml/FrameLift/Controls/FLListState.qml
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
        Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Multimedia Qt6::Widgets Qt6::Sql)
if (FRAMELIFT_MODULE_AI)
    target_link_libraries(FrameLift PRIVATE llama ${FRAMELIFT_MTMD_LIBRARY} Qt6::Network)
endif ()

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
    set_property(GLOBAL PROPERTY FRAMELIFT_PLUGIN_BINARY_NAMES)
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
    get_property(_framelift_plugin_binary_names GLOBAL PROPERTY FRAMELIFT_PLUGIN_BINARY_NAMES)
    set(_framelift_expected_plugin_files)
    foreach (_plugin_binary_name IN LISTS _framelift_plugin_binary_names)
        list(APPEND _framelift_expected_plugin_files "${_plugin_binary_name}${CMAKE_SHARED_LIBRARY_SUFFIX}")
    endforeach ()
    add_custom_target(framelift_prune_plugin_output
            COMMAND ${CMAKE_COMMAND}
                    "-DPLUGIN_DIR=$<TARGET_FILE_DIR:FrameLift>/plugins"
                    "-DPLUGIN_SUFFIX=${CMAKE_SHARED_LIBRARY_SUFFIX}"
                    "-DEXPECTED_PLUGIN_FILES=${_framelift_expected_plugin_files}"
                    -P "${CMAKE_SOURCE_DIR}/cmake/PrunePluginOutput.cmake"
            VERBATIM)
    add_dependencies(FrameLift framelift_prune_plugin_output)
    if (FRAMELIFT_PLUGIN_TARGETS)
        foreach (_plugin IN LISTS FRAMELIFT_PLUGIN_TARGETS)
            add_dependencies(${_plugin} framelift_prune_plugin_output)
        endforeach ()
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
    find_package(Threads REQUIRED)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(framelift_test_packet_queue tests/unit/media/FFmpegPacketQueueTests.cpp)
    set_target_properties(framelift_test_packet_queue PROPERTIES
            CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    target_include_directories(framelift_test_packet_queue PRIVATE ${FRAMELIFT_HOST_MODULE_INCLUDE_DIRS})
    target_link_libraries(framelift_test_packet_queue PRIVATE Threads::Threads ffmpeg)
    add_test(NAME framelift_packet_queue_generation COMMAND framelift_test_packet_queue)
    set_tests_properties(framelift_packet_queue_generation PROPERTIES LABELS "launch;unit" TIMEOUT 10)

    if (FRAMELIFT_MODULE_FRAME_SAMPLER)
        add_executable(framelift_test_frame_sampler
                tests/integration/FrameSamplerIntegrationTests.cpp
                modules/host/frame-sampler/FrameSamplerService.cpp
                src/TestMediaGenerator.cpp
                sdk/src/Log.cpp)
        set_target_properties(framelift_test_frame_sampler PROPERTIES
                CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF AUTOMOC ON)
        target_include_directories(framelift_test_frame_sampler PRIVATE
                "${CMAKE_SOURCE_DIR}/sdk/include"
                "${CMAKE_SOURCE_DIR}/src"
                ${FRAMELIFT_HOST_MODULE_INCLUDE_DIRS})
        target_link_libraries(framelift_test_frame_sampler PRIVATE
                FrameLiftSdk Threads::Threads Qt6::Core Qt6::Test ffmpeg)
        add_test(NAME framelift_frame_sampler_integration COMMAND framelift_test_frame_sampler)
        set_tests_properties(framelift_frame_sampler_integration PROPERTIES
                LABELS "launch;integration"
                TIMEOUT 35
                ENVIRONMENT
                "XDG_CACHE_HOME=${CMAKE_BINARY_DIR}/test-cache/framelift_frame_sampler")
    endif ()

    set(_framelift_launch_env
            "QT_QPA_PLATFORM=offscreen"
            "QT_QUICK_BACKEND=software"
            "FL_BACKEND=gl"
            "FL_ACCEL_MODE=off"
            "FL_LOG_LEVEL=debug"
            "FL_LOG_PERF=1"
            "FL_TEST_EXIT_AFTER_MS=1500"
            "XDG_CONFIG_HOME=${CMAKE_BINARY_DIR}/test-config/framelift_launch_qml_software")
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
    add_test(NAME framelift_launch_qml_software
            COMMAND $<TARGET_FILE:FrameLift>)
    set_tests_properties(framelift_launch_qml_software PROPERTIES
            WORKING_DIRECTORY "$<TARGET_FILE_DIR:FrameLift>"
            LABELS launch
            TIMEOUT 20
            ENVIRONMENT "${_framelift_launch_env}"
            PASS_REGULAR_EXPRESSION "startup-first-frame [0-9]+\\.[0-9]+ ms"
            FAIL_REGULAR_EXPRESSION "${_framelift_launch_fail_regex}")

    set(_framelift_opengl_launch_env
            "QT_QPA_PLATFORM=xcb"
            "QSG_RHI_BACKEND=opengl"
            "LIBGL_ALWAYS_SOFTWARE=1"
            "FL_BACKEND=gl"
            "FL_ACCEL_MODE=off"
            "FL_LOG_LEVEL=debug"
            "FL_LOG_PERF=1"
            "FL_TEST_FILE=2s-360p-mpeg4-noaudio"
            "FL_TEST_EXIT_AFTER_MS=3000"
            "XDG_CONFIG_HOME=${CMAKE_BINARY_DIR}/test-config/framelift_launch_opengl"
            "XDG_CACHE_HOME=${CMAKE_BINARY_DIR}/test-cache/framelift_launch_opengl")
    add_test(NAME framelift_launch_opengl
            COMMAND ${CMAKE_COMMAND}
                    "-DTEST_EXECUTABLE=$<TARGET_FILE:FrameLift>"
                    "-DTEST_WORKING_DIRECTORY=$<TARGET_FILE_DIR:FrameLift>"
                    "-DTEST_ENVIRONMENT=${_framelift_opengl_launch_env}"
                    "-DREQUIRED_REGEXES=video renderer initialized: OpenGL;file-open [0-9]+\\.[0-9]+ ms"
                    "-DFORBIDDEN_REGEXES=${_framelift_launch_fail_regex};GlVideoRenderer:;video renderer init failed"
                    -P "${CMAKE_SOURCE_DIR}/tests/support/RunLaunchAndCheck.cmake")
    set_tests_properties(framelift_launch_opengl PROPERTIES LABELS launch TIMEOUT 35)

    set(_framelift_lazy_surfaces_env ${_framelift_launch_env})
    list(FILTER _framelift_lazy_surfaces_env EXCLUDE REGEX "^(FL_TEST_EXIT_AFTER_MS|XDG_CONFIG_HOME)=")
    list(APPEND _framelift_lazy_surfaces_env
            "FL_TEST_ACTIVATE_LAZY_SURFACES=1"
            "FL_TEST_EXIT_AFTER_MS=750"
            "XDG_CONFIG_HOME=${CMAKE_BINARY_DIR}/test-config/framelift_launch_lazy_surfaces")
    add_test(NAME framelift_launch_lazy_surfaces
            COMMAND $<TARGET_FILE:FrameLift>)
    set_tests_properties(framelift_launch_lazy_surfaces PROPERTIES
            WORKING_DIRECTORY "$<TARGET_FILE_DIR:FrameLift>"
            LABELS launch
            TIMEOUT 20
            ENVIRONMENT "${_framelift_lazy_surfaces_env}"
            PASS_REGULAR_EXPRESSION "test lazy plugin surfaces activated"
            FAIL_REGULAR_EXPRESSION "${_framelift_launch_fail_regex}")

    set(_framelift_event_pump_quit_env
            "QT_QPA_PLATFORM=offscreen"
            "QT_QUICK_BACKEND=software"
            "FL_BACKEND=gl"
            "FL_ACCEL_MODE=off"
            "FL_LOG_LEVEL=debug"
            "FL_TEST_QUIT_AFTER_MS=1500"
            "XDG_CONFIG_HOME=${CMAKE_BINARY_DIR}/test-config/framelift_launch_event_pump_quit")
    add_test(NAME framelift_launch_event_pump_quit
            COMMAND $<TARGET_FILE:FrameLift>)
    set_tests_properties(framelift_launch_event_pump_quit PROPERTIES
            WORKING_DIRECTORY "$<TARGET_FILE_DIR:FrameLift>"
            LABELS launch
            TIMEOUT 20
            ENVIRONMENT "${_framelift_event_pump_quit_env}"
            PASS_REGULAR_EXPRESSION "App: module shutdown complete"
            FAIL_REGULAR_EXPRESSION "${_framelift_launch_fail_regex}")

    # Real playback seek through the launch-test-only timer hook. The perf match
    # proves a post-seek frame was presented; timeout/teardown catches generation
    # waits that fail to wake at seek or EOF boundaries.
    set(_framelift_seek_env
            "QT_QPA_PLATFORM=offscreen"
            "QT_QUICK_BACKEND=software"
            "FL_BACKEND=gl"
            "FL_ACCEL_MODE=off"
            "FL_LOG_LEVEL=debug"
            "FL_LOG_PERF=1"
            "FL_TEST_FILE=30s-360p-mpeg4-noaudio"
            "FL_TEST_SEEK_AFTER_MS=900"
            "FL_TEST_SEEK_DELTA=2"
            "FL_TEST_SEEK_COUNT=4"
            "FL_TEST_SEEK_INTERVAL_MS=25"
            "FL_TEST_EXIT_AFTER_MS=3000"
            "XDG_CONFIG_HOME=${CMAKE_BINARY_DIR}/test-config/framelift_launch_seek_generation"
            "XDG_CACHE_HOME=${CMAKE_BINARY_DIR}/test-cache/framelift_launch_seek_generation")
    add_test(NAME framelift_launch_seek_generation COMMAND $<TARGET_FILE:FrameLift>)
    set_tests_properties(framelift_launch_seek_generation PROPERTIES
            WORKING_DIRECTORY "$<TARGET_FILE_DIR:FrameLift>"
            LABELS launch
            TIMEOUT 30
            ENVIRONMENT "${_framelift_seek_env}"
            PASS_REGULAR_EXPRESSION "seek:g[0-9]+ [0-9]+\\.[0-9]+ ms"
            FAIL_REGULAR_EXPRESSION "${_framelift_launch_fail_regex};master clock stalled")

    set(_framelift_paused_seek_env
            "QT_QPA_PLATFORM=offscreen"
            "QT_QUICK_BACKEND=software"
            "FL_BACKEND=gl"
            "FL_ACCEL_MODE=off"
            "FL_LOG_LEVEL=debug"
            "FL_LOG_PERF=1"
            "FL_TEST_FILE=10s-360p-mpeg4-noaudio-srt"
            "FL_TEST_SEEK_AFTER_MS=900"
            "FL_TEST_SEEK_ABSOLUTE=7.25"
            "FL_TEST_PAUSE_BEFORE_SEEK=1"
            "FL_TEST_EXIT_AFTER_MS=2500"
            "XDG_CONFIG_HOME=${CMAKE_BINARY_DIR}/test-config/framelift_launch_paused_seek"
            "XDG_CACHE_HOME=${CMAKE_BINARY_DIR}/test-cache/framelift_launch_paused_seek")
    add_test(NAME framelift_launch_paused_absolute_seek COMMAND $<TARGET_FILE:FrameLift>)
    set_tests_properties(framelift_launch_paused_absolute_seek PROPERTIES
            WORKING_DIRECTORY "$<TARGET_FILE_DIR:FrameLift>"
            LABELS launch
            TIMEOUT 30
            ENVIRONMENT "${_framelift_paused_seek_env}"
            PASS_REGULAR_EXPRESSION "seek:g[0-9]+ [0-9]+\\.[0-9]+ ms"
            FAIL_REGULAR_EXPRESSION "${_framelift_launch_fail_regex};master clock stalled")

    set(_framelift_eof_seek_env
            "QT_QPA_PLATFORM=offscreen"
            "QT_QUICK_BACKEND=software"
            "FL_BACKEND=gl"
            "FL_ACCEL_MODE=off"
            "FL_LOG_LEVEL=debug"
            "FL_LOG_PERF=1"
            "FL_TEST_FILE=2s-360p-mpeg4-noaudio"
            "FL_TEST_SEEK_AFTER_MS=1500"
            "FL_TEST_SEEK_ABSOLUTE=0.5"
            "FL_TEST_EXIT_AFTER_MS=3500"
            "XDG_CONFIG_HOME=${CMAKE_BINARY_DIR}/test-config/framelift_launch_eof_seek"
            "XDG_CACHE_HOME=${CMAKE_BINARY_DIR}/test-cache/framelift_launch_eof_seek")
    add_test(NAME framelift_launch_eof_seek_restart COMMAND $<TARGET_FILE:FrameLift>)
    set_tests_properties(framelift_launch_eof_seek_restart PROPERTIES
            WORKING_DIRECTORY "$<TARGET_FILE_DIR:FrameLift>"
            LABELS launch
            TIMEOUT 30
            ENVIRONMENT "${_framelift_eof_seek_env}"
            PASS_REGULAR_EXPRESSION "seek:g[0-9]+ [0-9]+\\.[0-9]+ ms"
            FAIL_REGULAR_EXPRESSION "${_framelift_launch_fail_regex};master clock stalled")

    # A relative step that reaches the known duration completes the first file
    # immediately. The second generated sibling proves Playlist received the
    # normal clean-EOF event and advanced rather than merely holding at EOF.
    set(_framelift_boundary_forward_env
            "QT_QPA_PLATFORM=offscreen"
            "QT_QUICK_BACKEND=software"
            "FL_BACKEND=gl"
            "FL_ACCEL_MODE=off"
            "FL_LOG_LEVEL=debug"
            "FL_LOG_PERF=1"
            "FL_TEST_FILE=2s-360p-mpeg4-noaudio"
            "FL_TEST_FILE_EXTRA=3s-360p-mpeg4-noaudio"
            "FL_TEST_SEEK_AFTER_MS=900"
            "FL_TEST_SEEK_DELTA=5"
            "FL_TEST_SEEK_COUNT=5"
            "FL_TEST_SEEK_INTERVAL_MS=25"
            "FL_TEST_PAUSE_BEFORE_SEEK=1"
            "FL_TEST_EXIT_AFTER_MS=2600"
            "XDG_CONFIG_HOME=${CMAKE_BINARY_DIR}/test-config/framelift_launch_boundary_forward"
            "XDG_CACHE_HOME=${CMAKE_BINARY_DIR}/test-cache/framelift_launch_boundary_forward")
    add_test(NAME framelift_launch_seek_forward_advances
            COMMAND ${CMAKE_COMMAND}
                    "-DTEST_EXECUTABLE=$<TARGET_FILE:FrameLift>"
                    "-DTEST_WORKING_DIRECTORY=$<TARGET_FILE_DIR:FrameLift>"
                    "-DTEST_ENVIRONMENT=${_framelift_boundary_forward_env}"
                    "-DREQUIRED_REGEXES=relative seek completed file at end;playback-summary reason=superseded"
                    "-DFORBIDDEN_REGEXES=${_framelift_launch_fail_regex};master clock stalled"
                    "-DCOUNT_REGEX=relative seek completed file at end"
                    "-DEXPECTED_COUNT=1"
                    -P "${CMAKE_SOURCE_DIR}/tests/support/RunLaunchAndCheck.cmake")
    set_tests_properties(framelift_launch_seek_forward_advances PROPERTIES LABELS launch TIMEOUT 30)

    # The first backward press reaches zero; its repeat stream must not create
    # additional generations/audio resets after the clock starts moving again.
    set(_framelift_boundary_back_env
            "QT_QPA_PLATFORM=offscreen"
            "QT_QUICK_BACKEND=software"
            "FL_BACKEND=gl"
            "FL_ACCEL_MODE=off"
            "FL_LOG_LEVEL=debug"
            "FL_LOG_PERF=1"
            # Keep CI independent of a host audio daemon. RequestSeek still runs
            # the exact audio discontinuity/generation path and emits its cutoff
            # timing even when the generated clip has no audio stream.
            "FL_TEST_FILE=4s-360p-mpeg4-noaudio"
            "FL_TEST_SEEK_AFTER_MS=700"
            "FL_TEST_SEEK_DELTA=-5"
            "FL_TEST_SEEK_COUNT=8"
            "FL_TEST_SEEK_INTERVAL_MS=25"
            "FL_TEST_EXIT_AFTER_MS=2200"
            "XDG_CONFIG_HOME=${CMAKE_BINARY_DIR}/test-config/framelift_launch_boundary_back"
            "XDG_CACHE_HOME=${CMAKE_BINARY_DIR}/test-cache/framelift_launch_boundary_back")
    add_test(NAME framelift_launch_seek_back_stops_cleanly
            COMMAND ${CMAKE_COMMAND}
                    "-DTEST_EXECUTABLE=$<TARGET_FILE:FrameLift>"
                    "-DTEST_WORKING_DIRECTORY=$<TARGET_FILE_DIR:FrameLift>"
                    "-DTEST_ENVIRONMENT=${_framelift_boundary_back_env}"
                    "-DREQUIRED_REGEXES=seek:g[0-9]+ [0-9]+\\.[0-9]+ ms"
                    "-DFORBIDDEN_REGEXES=${_framelift_launch_fail_regex};master clock stalled"
                    "-DCOUNT_REGEX=seek-audio-cut:g[0-9]+"
                    "-DEXPECTED_COUNT=1"
                    -P "${CMAKE_SOURCE_DIR}/tests/support/RunLaunchAndCheck.cmake")
    set_tests_properties(framelift_launch_seek_back_stops_cleanly PROPERTIES LABELS launch TIMEOUT 30)

    # A decoder-level audio format change (stereo 44.1k → mono 22.05k mid-mp3) must
    # rebuild the playback resampler, not crash: a mono frame pushed through a
    # stereo-configured SwrContext dereferences a plane that doesn't exist (also
    # reachable via mp3 false-sync frames after mid-frame AVI seeks). The clip is a
    # byte-concatenation of two encodes, which FL_TEST_FILE's grammar can't express,
    # so the wrapper's SETUP_SCRIPT generates it. On hosts without an audio device
    # the sink never opens (CI stays independent of an audio daemon) — the required
    # alternation accepts that path, and a crash still fails via the exit code.
    set(_framelift_audio_format_switch_env
            "QT_QPA_PLATFORM=offscreen"
            "QT_QUICK_BACKEND=software"
            "FL_BACKEND=gl"
            "FL_ACCEL_MODE=off"
            "FL_LOG_LEVEL=debug"
            "FL_TEST_EXIT_AFTER_MS=3500"
            "XDG_CONFIG_HOME=${CMAKE_BINARY_DIR}/test-config/framelift_launch_audio_format_switch"
            "XDG_CACHE_HOME=${CMAKE_BINARY_DIR}/test-cache/framelift_launch_audio_format_switch")
    add_test(NAME framelift_launch_audio_format_switch
            COMMAND ${CMAKE_COMMAND}
                    "-DTEST_EXECUTABLE=$<TARGET_FILE:FrameLift>"
                    "-DTEST_WORKING_DIRECTORY=$<TARGET_FILE_DIR:FrameLift>"
                    "-DTEST_ENVIRONMENT=${_framelift_audio_format_switch_env}"
                    "-DSETUP_SCRIPT=${CMAKE_SOURCE_DIR}/tests/support/GenerateFormatSwitchAudio.cmake"
                    "-DFORMAT_SWITCH_AUDIO_FILE=${CMAKE_BINARY_DIR}/test-media/format-switch.mp3"
                    "-DTEST_ARGS=${CMAKE_BINARY_DIR}/test-media/format-switch.mp3"
                    "-DREQUIRED_REGEXES=rebuilding resampler|QAudioSink start failed"
                    "-DFORBIDDEN_REGEXES=${_framelift_launch_fail_regex}"
                    -P "${CMAKE_SOURCE_DIR}/tests/support/RunLaunchAndCheck.cmake")
    set_tests_properties(framelift_launch_audio_format_switch PROPERTIES LABELS launch TIMEOUT 30)

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
