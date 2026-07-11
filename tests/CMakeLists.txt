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

# JSON — backs the host JsonServiceImpl (Qt6::Core QJson), compiled into the History
# test so its persistence round-trips exercise the real backend.
find_package(Qt6 REQUIRED COMPONENTS Core Gui Network Test Sql)

enable_testing()
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

set(FRAMELIFT_QT_TEST_RUNNER "${CMAKE_CURRENT_SOURCE_DIR}/support/QtTestRunner.cpp")

function(framelift_qt_discover_tests target)
    set(_FL_TEST_INCLUDE_FILE "${CMAKE_CURRENT_BINARY_DIR}/${target}_qt_tests.cmake")
    file(WRITE "${_FL_TEST_INCLUDE_FILE}" "")
    add_custom_target(${target}_qt_discover ALL
            COMMAND ${CMAKE_COMMAND}
            -D "TEST_TARGET=$<TARGET_FILE:${target}>"
            -D "TEST_WORKING_DIR=$<TARGET_FILE_DIR:${target}>"
            -D "TEST_INCLUDE_FILE=${_FL_TEST_INCLUDE_FILE}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/support/DiscoverQtTests.cmake"
            VERBATIM)
    add_dependencies(${target}_qt_discover ${target})
    set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${_FL_TEST_INCLUDE_FILE}")
endfunction()

# ── framelift_tests: pure-logic + platform-builder suites (no plugin entry symbols) ─
add_executable(framelift_tests
        unit/misc/CliTests.cpp
        unit/misc/TestMediaSpecTests.cpp
        unit/host/SettingsTests.cpp
        unit/host/AppStateStoreTests.cpp
        unit/host/MediaStoreTests.cpp
        unit/host/ModuleContextTests.cpp
        unit/host/PluginConfigTests.cpp
        unit/host/CommandRegistryTests.cpp
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
        unit/media/FFmpegTimelineTests.cpp
        unit/media/FFmpegVideoConfigTests.cpp
        unit/media/FFmpegErrorTests.cpp
        unit/media/FFmpegHwDecodeTests.cpp
        unit/media/FFmpegLetterboxTests.cpp
        unit/media/SwsColorspaceTests.cpp
        unit/gfx/GraphicsApiTests.cpp
        unit/gfx/VideoRenderCallbacksTests.cpp
        unit/media/FFmpegSubtitleBlendTests.cpp
        unit/media/DeferredSubtitlePreloadTests.cpp
        unit/media/FFmpegTrackLabelTests.cpp
        unit/media/PlayerEventSinkTests.cpp
        unit/media/VideoFrameGateTests.cpp
        unit/media/FFmpegSidecarScanTests.cpp
        unit/media/FFmpegSeekPlanTests.cpp
        unit/media/FFmpegTrackSelectTests.cpp
        unit/gfx/VulkanCapabilityPlanTests.cpp
        unit/gfx/VulkanColorMappingTests.cpp
        unit/gfx/YuvToRgbMatrixTests.cpp
        unit/gfx/VulkanDeviceSelectTests.cpp
        unit/gfx/VulkanQueueLockTests.cpp
        unit/gfx/VulkanRetireQueueTests.cpp
        unit/gfx/VulkanTextureRingTests.cpp
        unit/gfx/VulkanUploadPolicyTests.cpp
        ${FRAMELIFT_QT_TEST_RUNNER}
        "${FRAMELIFT_ROOT}/modules/host/settings/Settings.cpp"
        "${FRAMELIFT_ROOT}/modules/host/module-runtime/PluginConfig.cpp"
        "${FRAMELIFT_ROOT}/modules/host/module-runtime/CommandRegistry.cpp"
        "${FRAMELIFT_ROOT}/modules/host/module-runtime/ModuleContext.cpp"
        "${FRAMELIFT_ROOT}/modules/host/module-runtime/AppPaths.cpp"
        "${FRAMELIFT_ROOT}/modules/host/module-runtime/PluginCatalog.cpp"
        "${FRAMELIFT_ROOT}/modules/host/module-runtime/SettingsService.cpp"
        "${FRAMELIFT_ROOT}/modules/host/module-runtime/PluginMetadata.cpp"
        "${FRAMELIFT_ROOT}/modules/host/module-runtime/PluginResolver.cpp"
        "${FRAMELIFT_ROOT}/modules/host/services/AppStateStore.cpp"
        "${FRAMELIFT_ROOT}/modules/host/services/HotkeysImpl.cpp"
        "${FRAMELIFT_ROOT}/modules/host/services/MediaStoreImpl.cpp"
        "${FRAMELIFT_ROOT}/sdk/src/KeyNames.cpp"
        "${FRAMELIFT_ROOT}/sdk/src/Log.cpp"
        "${FRAMELIFT_ROOT}/src/TestMediaGenerator.cpp"
)
set_target_properties(framelift_tests PROPERTIES
        CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF
        AUTOMOC ON)
target_include_directories(framelift_tests PRIVATE
        "${FRAMELIFT_ROOT}/sdk/include"
        "${FRAMELIFT_ROOT}/src"
        ${FRAMELIFT_GRAPHICS_TEST_INCLUDE_DIRS}
        ${FRAMELIFT_HOST_MODULE_TEST_INCLUDE_DIRS}
        "${FRAMELIFT_ROOT}/tests/support"  # TempIni.h for PluginConfig/ModuleSettings tests
        "${FRAMELIFT_ROOT}/plugins/playlist"
)
target_link_libraries(framelift_tests PRIVATE Threads::Threads Qt6::Core Qt6::Gui Qt6::Test Qt6::Sql)
framelift_apply_coverage(framelift_tests)
framelift_qt_discover_tests(framelift_tests)

add_executable(framelift_tests_vulkan_off
        unit/gfx/VulkanModuleDisabledTests.cpp
        ${FRAMELIFT_QT_TEST_RUNNER}
        "${FRAMELIFT_ROOT}/modules/host/settings/Settings.cpp"
)
set_target_properties(framelift_tests_vulkan_off PROPERTIES
        CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF
        AUTOMOC ON)
target_compile_definitions(framelift_tests_vulkan_off PRIVATE FRAMELIFT_MODULE_GRAPHICS_VULKAN=0)
target_include_directories(framelift_tests_vulkan_off PRIVATE
        "${FRAMELIFT_ROOT}/sdk/include"
        "${FRAMELIFT_ROOT}/src"
        ${FRAMELIFT_GRAPHICS_TEST_INCLUDE_DIRS}
        ${FRAMELIFT_HOST_MODULE_TEST_INCLUDE_DIRS}
        "${FRAMELIFT_ROOT}/tests/support"
)
target_link_libraries(framelift_tests_vulkan_off PRIVATE Threads::Threads Qt6::Core Qt6::Test)
framelift_apply_coverage(framelift_tests_vulkan_off)
framelift_qt_discover_tests(framelift_tests_vulkan_off)

add_executable(framelift_test_ai_service
        unit/host/AIBackendLogTests.cpp
        unit/host/AIServiceTests.cpp
        support/fakes/FakeAIEngine.cpp
        ${FRAMELIFT_QT_TEST_RUNNER}
        "${FRAMELIFT_ROOT}/modules/host/ai/AIBackendLog.cpp"
        "${FRAMELIFT_ROOT}/modules/host/ai/AIModelCatalog.cpp"
        "${FRAMELIFT_ROOT}/modules/host/ai/AIService.cpp")
set_target_properties(framelift_test_ai_service PROPERTIES
        CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF AUTOMOC ON)
target_include_directories(framelift_test_ai_service PRIVATE
        "${FRAMELIFT_ROOT}/sdk/include"
        ${FRAMELIFT_HOST_MODULE_TEST_INCLUDE_DIRS}
        "${FRAMELIFT_ROOT}/tests/support"
        "${FRAMELIFT_ROOT}/tests/support/fakes")
target_link_libraries(framelift_test_ai_service PRIVATE Threads::Threads Qt6::Core Qt6::Network Qt6::Test)
framelift_apply_coverage(framelift_test_ai_service)
framelift_qt_discover_tests(framelift_test_ai_service)

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
    add_executable(${name} ${_FL_TEST_SOURCES} ${FRAMELIFT_SDK_TEST_SOURCES} ${FRAMELIFT_QT_TEST_RUNNER})
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
            "${FRAMELIFT_ROOT}/plugins/console"
            "${FRAMELIFT_ROOT}/plugins/ai-tagger"
            "${FRAMELIFT_ROOT}/plugins/dbviewer"
    )
    target_link_libraries(${name} PRIVATE
            Threads::Threads Qt6::Core Qt6::Gui Qt6::Test)
    framelift_apply_coverage(${name})
    framelift_qt_discover_tests(${name})
endfunction()

# ── Per-plugin behaviour suites ───────────────────────────────────────────────
# History persists through the host IMediaStore service; compile the real Qt SQL-backed
# MediaStoreImpl into the test so its persistence round-trips exercise the real backend.
add_framelift_plugin_test(framelift_test_history
        plugins/history/HistoryTests.cpp
        "${FRAMELIFT_ROOT}/plugins/history/History.cpp"
        "${FRAMELIFT_ROOT}/plugins/history/HistorySettings.cpp"
        "${FRAMELIFT_ROOT}/modules/host/services/MediaStoreImpl.cpp")
target_link_libraries(framelift_test_history PRIVATE Qt6::Core Qt6::Sql)

# AI Tagger: pure-logic core (scheduler, convergence, store, worker) exercised with a
# fake sampler + fake backend against the real SQL-backed store. The shared host llama
# engine and the QObject module are deliberately excluded so this stays llama-free.
add_framelift_plugin_test(framelift_test_aitagger
        plugins/ai-tagger/AITaggerTests.cpp
        "${FRAMELIFT_ROOT}/plugins/ai-tagger/SampleScheduler.cpp"
        "${FRAMELIFT_ROOT}/plugins/ai-tagger/TagStore.cpp"
        "${FRAMELIFT_ROOT}/plugins/ai-tagger/TagWorker.cpp"
        "${FRAMELIFT_ROOT}/plugins/ai-tagger/HostAIBackend.cpp"
        "${FRAMELIFT_ROOT}/modules/host/services/MediaStoreImpl.cpp")
target_link_libraries(framelift_test_aitagger PRIVATE Qt6::Core Qt6::Sql)

# Database Viewer: enumerates and projects the store through the real SQL-backed
# MediaStoreImpl, so compile it in and link Qt SQL like the History suite.
add_framelift_plugin_test(framelift_test_dbviewer
        plugins/dbviewer/DBViewerTests.cpp
        "${FRAMELIFT_ROOT}/plugins/dbviewer/DBViewer.cpp"
        "${FRAMELIFT_ROOT}/modules/host/services/MediaStoreImpl.cpp")
target_link_libraries(framelift_test_dbviewer PRIVATE Qt6::Core Qt6::Sql)

# Playlist + SettingsMenu drive the real ModuleContext, so they also compile
# the host plugin runtime and settings modules.
add_framelift_plugin_test(framelift_test_playlist
        plugins/playlist/PlaylistTests.cpp
        "${FRAMELIFT_ROOT}/plugins/playlist/Playlist.cpp"
        "${FRAMELIFT_ROOT}/plugins/playlist/PlaylistSettings.cpp"
        "${FRAMELIFT_ROOT}/modules/host/module-runtime/PluginConfig.cpp"
        "${FRAMELIFT_ROOT}/modules/host/module-runtime/CommandRegistry.cpp"
        "${FRAMELIFT_ROOT}/modules/host/module-runtime/ModuleContext.cpp"
        "${FRAMELIFT_ROOT}/modules/host/module-runtime/AppPaths.cpp"
        "${FRAMELIFT_ROOT}/modules/host/module-runtime/PluginCatalog.cpp"
        "${FRAMELIFT_ROOT}/modules/host/module-runtime/SettingsService.cpp"
        "${FRAMELIFT_ROOT}/modules/host/settings/Settings.cpp")

add_framelift_plugin_test(framelift_test_settingsmenu
        plugins/settings-menu/SettingsMenuTests.cpp
        "${FRAMELIFT_ROOT}/plugins/settings-menu/SettingsMenu.cpp"
        "${FRAMELIFT_ROOT}/modules/host/module-runtime/PluginConfig.cpp"
        "${FRAMELIFT_ROOT}/modules/host/module-runtime/CommandRegistry.cpp"
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
target_compile_definitions(framelift_test_benchmark PRIVATE FRAMELIFT_BENCHMARK_TESTS)
if (WIN32)
    target_link_libraries(framelift_test_benchmark PRIVATE pdh)
endif ()

add_framelift_plugin_test(framelift_test_remotestream
        plugins/remote-stream/RemoteStreamTests.cpp
        "${FRAMELIFT_ROOT}/plugins/remote-stream/RemoteStream.cpp")

add_framelift_plugin_test(framelift_test_contextmenu
        plugins/context-menu/ContextMenuTests.cpp
        "${FRAMELIFT_ROOT}/plugins/context-menu/ContextMenuModule.cpp")

add_framelift_plugin_test(framelift_test_console
        plugins/console/ConsoleTests.cpp
        "${FRAMELIFT_ROOT}/plugins/console/Console.cpp"
        "${FRAMELIFT_ROOT}/plugins/console/ConsoleSettings.cpp"
        "${FRAMELIFT_ROOT}/modules/host/module-runtime/CommandRegistry.cpp")
