# Unit-test project for FrameLift's platform-independent logic.
#
# Dual-mode:
#   • Standalone (CI):   cmake -S tests -B build-tests   — native compiler, no FFmpeg.
#   • In-tree (IDE/dev): the root CMakeLists `add_subdirectory(tests)` when
#     FRAMELIFT_BUILD_TESTS is ON, so the suite shows up in the solution/CMake view.
#
# The pure-logic suite (framelift_tests) compiles only standalone units. Plugin behaviour
# suites compile a single module entry .cpp each (one exe per plugin; each emits
# framelift_create/etc., so two in one binary would clash) plus the SDK helper sources.
if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.28)
    project(FrameLiftTests LANGUAGES CXX)
endif ()

include(FetchContent)
FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
        GIT_SHALLOW TRUE
)
# Match the test exe's CRT on MSVC (no effect on GCC/Clang).
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# JSON — backs the host JsonServiceImpl (Qt6::Core QJson), compiled into the History
# test so its persistence round-trips exercise the real backend.
find_package(Qt6 REQUIRED COMPONENTS Core Gui)

enable_testing()
include(GoogleTest)
find_package(Threads REQUIRED)

set(FRAMELIFT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
# Reuse the host/gfx include-dir lists (single source of truth) instead of re-listing
# them; resolve them against the repo root so the standalone tests build works too.
set(FRAMELIFT_SOURCE_ROOT "${FRAMELIFT_ROOT}")
include("${FRAMELIFT_ROOT}/cmake/FrameLiftHostIncludes.cmake")
set(FRAMELIFT_GRAPHICS_TEST_INCLUDE_DIRS ${FRAMELIFT_GRAPHICS_INCLUDE_DIRS})
set(FRAMELIFT_HOST_MODULE_TEST_INCLUDE_DIRS ${FRAMELIFT_HOST_MODULE_INCLUDE_DIRS})

# ── Coverage (gcov/gcovr) ─────────────────────────────────────────────────────
option(FRAMELIFT_COVERAGE "Instrument the tests for gcov/gcovr coverage" OFF)
function(framelift_apply_coverage tgt)
    if (FRAMELIFT_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${tgt} PRIVATE --coverage -O0 -g)
        target_link_options(${tgt} PRIVATE --coverage)
    endif ()
endfunction()

# SDK helper sources compiled into every plugin (and thus every plugin test).
set(FRAMELIFT_SDK_TEST_SOURCES
        "${FRAMELIFT_ROOT}/sdk/src/ModuleBase.cpp"
        "${FRAMELIFT_ROOT}/sdk/src/KeyNames.cpp"
        "${FRAMELIFT_ROOT}/sdk/src/Log.cpp"
)

# ── framelift_tests: pure-logic + platform-builder suites (no plugin entry symbols) ─
add_executable(framelift_tests
        unit/misc/CliTests.cpp
        unit/host/SettingsTests.cpp
        unit/host/ModuleContextTests.cpp
        unit/host/PluginConfigTests.cpp
        unit/host/ModuleSettingsTests.cpp
        unit/misc/KeyNamesTests.cpp
        unit/host/HotkeysTests.cpp
        unit/host/PluginAbiTests.cpp
        unit/host/PluginResolverTests.cpp
        unit/misc/UrlUtilsTests.cpp
        unit/misc/ExtensionFilterTests.cpp
        unit/media/FFmpegFiltersTests.cpp
        unit/media/MediaEventTests.cpp
        unit/host/SettingsMappingTests.cpp
        unit/host/ReadAheadCacheTests.cpp
        unit/misc/ThemeUtilTests.cpp
        unit/media/FFmpegClockTests.cpp
        unit/media/FFmpegErrorTests.cpp
        unit/media/FFmpegHwDecodeTests.cpp
        unit/media/FFmpegLetterboxTests.cpp
        unit/gfx/GraphicsApiTests.cpp
        unit/media/FFmpegSubtitleBlendTests.cpp
        unit/media/FFmpegTrackLabelTests.cpp
        unit/gfx/VulkanQueueLockTests.cpp
        "${FRAMELIFT_ROOT}/modules/host/settings/Settings.cpp"
        "${FRAMELIFT_ROOT}/modules/host/module-runtime/PluginConfig.cpp"
        "${FRAMELIFT_ROOT}/modules/host/module-runtime/ModuleContext.cpp"
        "${FRAMELIFT_ROOT}/modules/host/module-runtime/AppPaths.cpp"
        "${FRAMELIFT_ROOT}/modules/host/module-runtime/PluginCatalog.cpp"
        "${FRAMELIFT_ROOT}/modules/host/module-runtime/SettingsService.cpp"
        "${FRAMELIFT_ROOT}/modules/host/module-runtime/PluginMetadata.cpp"
        "${FRAMELIFT_ROOT}/modules/host/module-runtime/PluginResolver.cpp"
        "${FRAMELIFT_ROOT}/modules/host/services/HotkeysImpl.cpp"
        "${FRAMELIFT_ROOT}/sdk/src/KeyNames.cpp"
        "${FRAMELIFT_ROOT}/sdk/src/Log.cpp"
)
set_target_properties(framelift_tests PROPERTIES
        CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_include_directories(framelift_tests PRIVATE
        "${FRAMELIFT_ROOT}/sdk/include"
        "${FRAMELIFT_ROOT}/src"
        ${FRAMELIFT_GRAPHICS_TEST_INCLUDE_DIRS}
        ${FRAMELIFT_HOST_MODULE_TEST_INCLUDE_DIRS}
        "${FRAMELIFT_ROOT}/tests/support"  # TempIni.h for PluginConfig/ModuleSettings tests
        "${FRAMELIFT_ROOT}/plugins/playlist"
)
target_link_libraries(framelift_tests PRIVATE GTest::gtest_main GTest::gmock Threads::Threads Qt6::Core Qt6::Gui)
framelift_apply_coverage(framelift_tests)
gtest_discover_tests(framelift_tests)

add_executable(framelift_tests_vulkan_off
        unit/gfx/VulkanModuleDisabledTests.cpp
        "${FRAMELIFT_ROOT}/modules/host/settings/Settings.cpp"
)
set_target_properties(framelift_tests_vulkan_off PROPERTIES
        CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_compile_definitions(framelift_tests_vulkan_off PRIVATE FRAMELIFT_MODULE_GRAPHICS_VULKAN=0)
target_include_directories(framelift_tests_vulkan_off PRIVATE
        "${FRAMELIFT_ROOT}/sdk/include"
        "${FRAMELIFT_ROOT}/src"
        ${FRAMELIFT_GRAPHICS_TEST_INCLUDE_DIRS}
        ${FRAMELIFT_HOST_MODULE_TEST_INCLUDE_DIRS}
)
target_link_libraries(framelift_tests_vulkan_off PRIVATE GTest::gtest_main Threads::Threads Qt6::Core)
framelift_apply_coverage(framelift_tests_vulkan_off)
gtest_discover_tests(framelift_tests_vulkan_off)

# ── Helper: one test exe per plugin ───────────────────────────────────────────
# add_framelift_plugin_test(NAME [EMIT_MODULE_ENTRY] <test.cpp> <plugin.cpp> [extra .cpp]...)
function(add_framelift_plugin_test name)
    cmake_parse_arguments(_FL_TEST "EMIT_MODULE_ENTRY" "" "" ${ARGN})
    set(_FL_TEST_METADATA_JSON "${CMAKE_CURRENT_BINARY_DIR}/${name}PluginMetadata.json")
    file(WRITE "${_FL_TEST_METADATA_JSON}" "{}\n")
    set(_FL_TEST_SOURCES ${_FL_TEST_UNPARSED_ARGUMENTS})
    foreach (_FL_TEST_SOURCE IN LISTS _FL_TEST_UNPARSED_ARGUMENTS)
        if (_FL_TEST_SOURCE MATCHES "\\.cpp$")
            if (IS_ABSOLUTE "${_FL_TEST_SOURCE}")
                set(_FL_TEST_SOURCE_ABS "${_FL_TEST_SOURCE}")
            else ()
                set(_FL_TEST_SOURCE_ABS "${CMAKE_CURRENT_SOURCE_DIR}/${_FL_TEST_SOURCE}")
            endif ()
            string(REGEX REPLACE "\\.cpp$" ".h" _FL_TEST_HEADER "${_FL_TEST_SOURCE_ABS}")
            if (EXISTS "${_FL_TEST_HEADER}")
                list(APPEND _FL_TEST_SOURCES "${_FL_TEST_HEADER}")
            endif ()
        endif ()
    endforeach ()
    add_executable(${name} ${_FL_TEST_SOURCES} ${FRAMELIFT_SDK_TEST_SOURCES})
    set_target_properties(${name} PROPERTIES
            CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF
            AUTOMOC ON)
    target_compile_definitions(${name} PRIVATE
            FRAMELIFT_BUILDING_MODULE
            FRAMELIFT_PLUGIN_METADATA_JSON="${name}PluginMetadata.json"
            QT_NO_KEYWORDS)
    if (NOT _FL_TEST_EMIT_MODULE_ENTRY)
        target_compile_definitions(${name} PRIVATE FRAMELIFT_SUPPRESS_MODULE_ENTRY)
    endif ()
    target_include_directories(${name} PRIVATE
            "${FRAMELIFT_ROOT}/sdk/include"
            "${FRAMELIFT_ROOT}/src"
            ${FRAMELIFT_GRAPHICS_TEST_INCLUDE_DIRS}
                ${FRAMELIFT_HOST_MODULE_TEST_INCLUDE_DIRS}
            "${FRAMELIFT_ROOT}/tests/support"
            "${FRAMELIFT_ROOT}/tests/support/stubs"  # stub Version.h for plugins that need generated headers
            "${FRAMELIFT_ROOT}/plugins/playlist"
            "${FRAMELIFT_ROOT}/plugins/history"
            "${FRAMELIFT_ROOT}/plugins/settings-menu"
            "${FRAMELIFT_ROOT}/plugins/overlay"
            "${FRAMELIFT_ROOT}/plugins/debug-overlay"
            "${FRAMELIFT_ROOT}/plugins/benchmark"
            "${FRAMELIFT_ROOT}/plugins/remote-stream"
            "${FRAMELIFT_ROOT}/plugins/context-menu"
            "${FRAMELIFT_ROOT}/plugins/log-viewer"
    )
    target_link_libraries(${name} PRIVATE
            GTest::gtest_main GTest::gmock Threads::Threads Qt6::Core Qt6::Gui)
    framelift_apply_coverage(${name})
    gtest_discover_tests(${name})
endfunction()

# ── Per-plugin behaviour suites ───────────────────────────────────────────────
# History persists through the host IJson service; compile the real QJson-backed
# JsonServiceImpl into the test (the only suite that exercises JSON) and link Qt6::Core.
add_framelift_plugin_test(framelift_test_history
        plugins/history/HistoryTests.cpp
        "${FRAMELIFT_ROOT}/plugins/history/History.cpp"
        "${FRAMELIFT_ROOT}/plugins/history/HistorySettings.cpp"
        "${FRAMELIFT_ROOT}/modules/host/services/JsonServiceImpl.cpp")
target_link_libraries(framelift_test_history PRIVATE Qt6::Core)

# Playlist + SettingsMenu drive the real ModuleContext, so they also compile
# the host plugin runtime and settings modules.
add_framelift_plugin_test(framelift_test_playlist
        plugins/playlist/PlaylistTests.cpp
        "${FRAMELIFT_ROOT}/plugins/playlist/Playlist.cpp"
        "${FRAMELIFT_ROOT}/plugins/playlist/PlaylistSettings.cpp"
        "${FRAMELIFT_ROOT}/modules/host/module-runtime/PluginConfig.cpp"
        "${FRAMELIFT_ROOT}/modules/host/module-runtime/ModuleContext.cpp"
        "${FRAMELIFT_ROOT}/modules/host/module-runtime/AppPaths.cpp"
        "${FRAMELIFT_ROOT}/modules/host/module-runtime/PluginCatalog.cpp"
        "${FRAMELIFT_ROOT}/modules/host/module-runtime/SettingsService.cpp"
        "${FRAMELIFT_ROOT}/modules/host/settings/Settings.cpp")

add_framelift_plugin_test(framelift_test_settingsmenu
        plugins/settings-menu/SettingsMenuTests.cpp
        "${FRAMELIFT_ROOT}/plugins/settings-menu/SettingsMenu.cpp"
        "${FRAMELIFT_ROOT}/modules/host/module-runtime/PluginConfig.cpp"
        "${FRAMELIFT_ROOT}/modules/host/module-runtime/ModuleContext.cpp"
        "${FRAMELIFT_ROOT}/modules/host/module-runtime/AppPaths.cpp"
        "${FRAMELIFT_ROOT}/modules/host/module-runtime/PluginCatalog.cpp"
        "${FRAMELIFT_ROOT}/modules/host/module-runtime/SettingsService.cpp"
        "${FRAMELIFT_ROOT}/modules/host/settings/Settings.cpp")

add_framelift_plugin_test(framelift_test_overlay
        plugins/overlay/OverlayTests.cpp
        "${FRAMELIFT_ROOT}/plugins/overlay/Overlay.cpp")

add_framelift_plugin_test(framelift_test_debugoverlay
        plugins/debug-overlay/DebugOverlayTests.cpp
        "${FRAMELIFT_ROOT}/plugins/debug-overlay/DebugOverlay.cpp")

add_framelift_plugin_test(framelift_test_benchmark
        plugins/benchmark/BenchmarkTests.cpp
        "${FRAMELIFT_ROOT}/plugins/benchmark/Benchmark.cpp"
        "${FRAMELIFT_ROOT}/plugins/benchmark/BenchmarkSettings.cpp"
        "${FRAMELIFT_ROOT}/plugins/benchmark/SysStats.cpp")
if (WIN32)
    target_link_libraries(framelift_test_benchmark PRIVATE pdh)
endif ()

add_framelift_plugin_test(framelift_test_remotestream
        plugins/remote-stream/RemoteStreamTests.cpp
        "${FRAMELIFT_ROOT}/plugins/remote-stream/RemoteStream.cpp")

add_framelift_plugin_test(framelift_test_contextmenu
        plugins/context-menu/ContextMenuTests.cpp
        "${FRAMELIFT_ROOT}/plugins/context-menu/ContextMenuModule.cpp")

add_framelift_plugin_test(framelift_test_logviewer
        plugins/log-viewer/LogViewerTests.cpp
        "${FRAMELIFT_ROOT}/plugins/log-viewer/LogViewer.cpp"
        "${FRAMELIFT_ROOT}/plugins/log-viewer/LogViewerSettings.cpp")
