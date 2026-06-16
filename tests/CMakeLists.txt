# Unit-test project for FrameLift's platform-independent logic.
#
# Dual-mode:
#   • Standalone (CI):   cmake -S tests -B build-tests   — native compiler, no SDL/FFmpeg.
#   • In-tree (IDE/dev): the root CMakeLists `add_subdirectory(tests)` when
#     FRAMELIFT_BUILD_TESTS is ON, so the suite shows up in the solution/CMake view.
#
# The pure-logic suite (framelift_tests) compiles only standalone units. Plugin behaviour
# suites compile a single plugin .cpp each (one exe per plugin — each plugin exports
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

# JSON — needed by plugins that persist data (History).
FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
        GIT_SHALLOW TRUE
)
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

enable_testing()
include(GoogleTest)
find_package(Threads REQUIRED)

set(FRAMELIFT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

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
        "${FRAMELIFT_ROOT}/sdk/src/PluginBase.cpp"
        "${FRAMELIFT_ROOT}/sdk/src/Panel.cpp"
        "${FRAMELIFT_ROOT}/sdk/src/Widgets.cpp"
        "${FRAMELIFT_ROOT}/sdk/src/KeyNames.cpp"
        "${FRAMELIFT_ROOT}/sdk/src/Log.cpp"
)

# ── framelift_tests: pure-logic + platform-builder suites (no plugin exports) ───────
add_executable(framelift_tests
        CliTests.cpp
        SettingsTests.cpp
        PluginContextTests.cpp
        PluginSettingsTests.cpp
        FocusManagerTests.cpp
        KeyNamesTests.cpp
        SemVerTests.cpp
        PluginAbiTests.cpp
        UrlUtilsTests.cpp
        ExtensionFilterTests.cpp
        FFmpegFiltersTests.cpp
        MediaEventTests.cpp
        SettingsMappingTests.cpp
        ReadAheadCacheTests.cpp
        ThemeUtilTests.cpp
        FontScanTests.cpp
        FFmpegClockTests.cpp
        FFmpegHwDecodeTests.cpp
        FFmpegLetterboxTests.cpp
        GraphicsApiTests.cpp
        FFmpegSubtitleBlendTests.cpp
        FFmpegTrackLabelTests.cpp
        "${FRAMELIFT_ROOT}/src/Settings.cpp"
        "${FRAMELIFT_ROOT}/src/PluginContext.cpp"
        "${FRAMELIFT_ROOT}/src/FontScan.cpp"
        "${FRAMELIFT_ROOT}/sdk/src/KeyNames.cpp"
)
set_target_properties(framelift_tests PROPERTIES
        CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_include_directories(framelift_tests PRIVATE
        "${FRAMELIFT_ROOT}/sdk/include"
        "${FRAMELIFT_ROOT}/src"
        "${FRAMELIFT_ROOT}/plugins/Updater"
        "${FRAMELIFT_ROOT}/plugins/Playlist"
)
target_link_libraries(framelift_tests PRIVATE GTest::gtest_main GTest::gmock Threads::Threads)
framelift_apply_coverage(framelift_tests)
gtest_discover_tests(framelift_tests)

# ── Helper: one test exe per plugin ───────────────────────────────────────────
# add_framelift_plugin_test(NAME <test.cpp> <plugin.cpp> [extra .cpp]...)
function(add_framelift_plugin_test name)
    add_executable(${name} ${ARGN} ${FRAMELIFT_SDK_TEST_SOURCES})
    set_target_properties(${name} PROPERTIES
            CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    target_compile_definitions(${name} PRIVATE FRAMELIFT_BUILDING_PLUGIN)
    target_include_directories(${name} PRIVATE
            "${FRAMELIFT_ROOT}/sdk/include"
            "${FRAMELIFT_ROOT}/src"
            "${FRAMELIFT_ROOT}/tests"
            "${FRAMELIFT_ROOT}/tests/stubs"  # stub IconData.h / Version.h for plugins that need generated headers
            "${FRAMELIFT_ROOT}/plugins/Playlist"
            "${FRAMELIFT_ROOT}/plugins/History"
            "${FRAMELIFT_ROOT}/plugins/SettingsMenu"
            "${FRAMELIFT_ROOT}/plugins/Overlay"
            "${FRAMELIFT_ROOT}/plugins/DebugOverlay"
            "${FRAMELIFT_ROOT}/plugins/Benchmark"
            "${FRAMELIFT_ROOT}/plugins/RemoteStream"
    )
    target_link_libraries(${name} PRIVATE
            GTest::gtest_main GTest::gmock Threads::Threads nlohmann_json::nlohmann_json)
    framelift_apply_coverage(${name})
    gtest_discover_tests(${name})
endfunction()

# ── Export macro suites ───────────────────────────────────────────────────────
# Each invokes FRAMELIFT_PLUGIN_EXPORT on a dummy plugin and calls the generated
# extern "C" entry points directly — two exes because the C symbols would clash.
add_framelift_plugin_test(framelift_test_exports
        PluginExportTests.cpp)

add_framelift_plugin_test(framelift_test_exports_norender
        PluginExportNoRenderTests.cpp)

# ── Per-plugin behaviour suites ───────────────────────────────────────────────
add_framelift_plugin_test(framelift_test_history
        HistoryTests.cpp
        "${FRAMELIFT_ROOT}/plugins/History/History.cpp")

# Playlist + SettingsMenu drive the real PluginContext, so they also compile
# src/PluginContext.cpp + src/Settings.cpp.
add_framelift_plugin_test(framelift_test_playlist
        PlaylistTests.cpp
        "${FRAMELIFT_ROOT}/plugins/Playlist/Playlist.cpp"
        "${FRAMELIFT_ROOT}/src/PluginContext.cpp"
        "${FRAMELIFT_ROOT}/src/FontScan.cpp"
        "${FRAMELIFT_ROOT}/src/Settings.cpp")

add_framelift_plugin_test(framelift_test_settingsmenu
        SettingsMenuTests.cpp
        "${FRAMELIFT_ROOT}/plugins/SettingsMenu/SettingsMenu.cpp"
        "${FRAMELIFT_ROOT}/src/PluginContext.cpp"
        "${FRAMELIFT_ROOT}/src/FontScan.cpp"
        "${FRAMELIFT_ROOT}/src/Settings.cpp")

add_framelift_plugin_test(framelift_test_overlay
        OverlayTests.cpp
        "${FRAMELIFT_ROOT}/plugins/Overlay/Overlay.cpp")

add_framelift_plugin_test(framelift_test_debugoverlay
        DebugOverlayTests.cpp
        "${FRAMELIFT_ROOT}/plugins/DebugOverlay/DebugOverlay.cpp")

add_framelift_plugin_test(framelift_test_benchmark
        BenchmarkTests.cpp
        "${FRAMELIFT_ROOT}/plugins/Benchmark/Benchmark.cpp"
        "${FRAMELIFT_ROOT}/plugins/Benchmark/SysStats.cpp")

add_framelift_plugin_test(framelift_test_remotestream
        RemoteStreamTests.cpp
        "${FRAMELIFT_ROOT}/plugins/RemoteStream/RemoteStream.cpp")
