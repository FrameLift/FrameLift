# Unit-test project for FrameLift's platform-independent logic.
#
# Dual-mode:
#   • Standalone (CI):   cmake -S tests -B build-tests   — native compiler, no SDL/FFmpeg.
#   • In-tree (IDE/dev): the root CMakeLists `add_subdirectory(tests)` when
#     FRAMELIFT_BUILD_TESTS is ON, so the suite shows up in the solution/CMake view.
#
# The pure-logic suite (framelift_tests) compiles only standalone units. Plugin behaviour
# suites compile a single module entry .cpp each (one exe per package; each emits
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
set(FRAMELIFT_GRAPHICS_TEST_INCLUDE_DIRS
        "${FRAMELIFT_ROOT}/modules/gfx/graphics-core"
        "${FRAMELIFT_ROOT}/modules/gfx/opengl"
        "${FRAMELIFT_ROOT}/modules/gfx/vulkan"
)
set(FRAMELIFT_HOST_MODULE_TEST_INCLUDE_DIRS
        "${FRAMELIFT_ROOT}/modules/host/module-runtime"
        "${FRAMELIFT_ROOT}/modules/host/settings"
        "${FRAMELIFT_ROOT}/modules/host/services"
        "${FRAMELIFT_ROOT}/modules/host/logging"
        "${FRAMELIFT_ROOT}/modules/host/audio"
        "${FRAMELIFT_ROOT}/modules/host/playback"
        "${FRAMELIFT_ROOT}/modules/host/read-ahead"
        "${FRAMELIFT_ROOT}/modules/host/ui"
        "${FRAMELIFT_ROOT}/modules/platform/window-sdl"
        "${FRAMELIFT_ROOT}/modules/platform/dir-watch"
        "${FRAMELIFT_ROOT}/modules/platform/win-shell"
)

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

# ── framelift_tests: pure-logic + platform-builder suites (no package entry symbols) ─
add_executable(framelift_tests
        unit/misc/CliTests.cpp
        unit/host/SettingsTests.cpp
        unit/host/ModuleContextTests.cpp
        unit/host/PackageConfigTests.cpp
        unit/host/ModuleSettingsTests.cpp
        unit/host/FocusManagerTests.cpp
        unit/misc/KeyNamesTests.cpp
        unit/host/SemVerTests.cpp
        unit/host/HotkeysTests.cpp
        unit/host/PackageAbiTests.cpp
        unit/host/PackageResolverTests.cpp
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
        "${FRAMELIFT_ROOT}/modules/host/module-runtime/PackageConfig.cpp"
        "${FRAMELIFT_ROOT}/modules/host/module-runtime/ModuleContext.cpp"
        "${FRAMELIFT_ROOT}/modules/host/module-runtime/PackageResolver.cpp"
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
        "${FRAMELIFT_ROOT}/tests/support"  # TempIni.h for PackageConfig/ModuleSettings tests
        "${FRAMELIFT_ROOT}/plugins/updater/core"
        "${FRAMELIFT_ROOT}/plugins/playlist/core"
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
# add_framelift_plugin_test(NAME [EMIT_MODULE_ENTRY] [METADATA_HEADER <header>] <test.cpp> <plugin.cpp> [extra .cpp]...)
function(add_framelift_plugin_test name)
    cmake_parse_arguments(_FL_TEST "EMIT_MODULE_ENTRY" "METADATA_HEADER" "" ${ARGN})
    if (NOT _FL_TEST_METADATA_HEADER)
        set(_FL_TEST_METADATA_HEADER "TestPackageMetadata.h")
    endif ()
    add_executable(${name} ${_FL_TEST_UNPARSED_ARGUMENTS} ${FRAMELIFT_SDK_TEST_SOURCES})
    set_target_properties(${name} PROPERTIES
            CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    target_compile_definitions(${name} PRIVATE
            FRAMELIFT_BUILDING_MODULE
            FRAMELIFT_MODULE_METADATA_HEADER="${_FL_TEST_METADATA_HEADER}")
    if (NOT _FL_TEST_EMIT_MODULE_ENTRY)
        target_compile_definitions(${name} PRIVATE FRAMELIFT_SUPPRESS_MODULE_ENTRY)
    endif ()
    target_include_directories(${name} PRIVATE
            "${FRAMELIFT_ROOT}/sdk/include"
            "${FRAMELIFT_ROOT}/src"
            ${FRAMELIFT_GRAPHICS_TEST_INCLUDE_DIRS}
                ${FRAMELIFT_HOST_MODULE_TEST_INCLUDE_DIRS}
            "${FRAMELIFT_ROOT}/tests/support"
            "${FRAMELIFT_ROOT}/tests/support/stubs"  # stub IconData.h / Version.h for plugins that need generated headers
            "${FRAMELIFT_ROOT}/plugins/playlist/core"
            "${FRAMELIFT_ROOT}/plugins/history/core"
            "${FRAMELIFT_ROOT}/plugins/settings-menu/core"
            "${FRAMELIFT_ROOT}/plugins/overlay/core"
            "${FRAMELIFT_ROOT}/plugins/debug-overlay/core"
            "${FRAMELIFT_ROOT}/plugins/benchmark/core"
            "${FRAMELIFT_ROOT}/plugins/remote-stream/core"
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
        "${FRAMELIFT_ROOT}/plugins/history/core/History.cpp"
        "${FRAMELIFT_ROOT}/modules/host/services/JsonServiceImpl.cpp")
target_link_libraries(framelift_test_history PRIVATE Qt6::Core)

# Playlist + SettingsMenu drive the real ModuleContext, so they also compile
# the host plugin runtime and settings modules.
add_framelift_plugin_test(framelift_test_playlist
        plugins/playlist/PlaylistTests.cpp
        "${FRAMELIFT_ROOT}/plugins/playlist/core/Playlist.cpp"
        "${FRAMELIFT_ROOT}/modules/host/module-runtime/PackageConfig.cpp"
        "${FRAMELIFT_ROOT}/modules/host/module-runtime/ModuleContext.cpp"
        "${FRAMELIFT_ROOT}/modules/host/settings/Settings.cpp")

add_framelift_plugin_test(framelift_test_settingsmenu
        plugins/settings-menu/SettingsMenuTests.cpp
        "${FRAMELIFT_ROOT}/plugins/settings-menu/core/SettingsMenu.cpp"
        "${FRAMELIFT_ROOT}/modules/host/module-runtime/PackageConfig.cpp"
        "${FRAMELIFT_ROOT}/modules/host/module-runtime/ModuleContext.cpp"
        "${FRAMELIFT_ROOT}/modules/host/settings/Settings.cpp")

add_framelift_plugin_test(framelift_test_overlay
        plugins/overlay/OverlayTests.cpp
        "${FRAMELIFT_ROOT}/plugins/overlay/core/Overlay.cpp")

add_framelift_plugin_test(framelift_test_debugoverlay
        plugins/debug-overlay/DebugOverlayTests.cpp
        "${FRAMELIFT_ROOT}/plugins/debug-overlay/core/DebugOverlay.cpp")

add_framelift_plugin_test(framelift_test_benchmark
        plugins/benchmark/BenchmarkTests.cpp
        "${FRAMELIFT_ROOT}/plugins/benchmark/core/Benchmark.cpp"
        "${FRAMELIFT_ROOT}/plugins/benchmark/core/SysStats.cpp")
if (WIN32)
    target_link_libraries(framelift_test_benchmark PRIVATE pdh)
endif ()

add_framelift_plugin_test(framelift_test_remotestream
        plugins/remote-stream/RemoteStreamTests.cpp
        "${FRAMELIFT_ROOT}/plugins/remote-stream/core/RemoteStream.cpp")
