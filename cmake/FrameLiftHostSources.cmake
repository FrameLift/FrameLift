# Explicit host source ownership for framelift app.
#
# Built-in modules are JSON-authored and reported by FrameLiftBuiltinModules.cmake.
# Source groups are appended by module state so lean builds do not compile code
# for disabled backends.

set(_FRAMELIFT_HOST_CORE_SOURCES
        "${CMAKE_SOURCE_DIR}/src/App.cpp"
        "${CMAKE_SOURCE_DIR}/src/App.h"
        "${CMAKE_SOURCE_DIR}/src/Cli.h"
        "${CMAKE_SOURCE_DIR}/src/main.cpp"
)

set(_FRAMELIFT_HOST_MODULE_RUNTIME_SOURCES
        "${CMAKE_SOURCE_DIR}/modules/host/module-runtime/PluginConfig.cpp"
        "${CMAKE_SOURCE_DIR}/modules/host/module-runtime/PluginConfig.h"
        "${CMAKE_SOURCE_DIR}/modules/host/module-runtime/CommandRegistry.cpp"
        "${CMAKE_SOURCE_DIR}/modules/host/module-runtime/CommandRegistry.h"
        "${CMAKE_SOURCE_DIR}/modules/host/module-runtime/ModuleContext.cpp"
        "${CMAKE_SOURCE_DIR}/modules/host/module-runtime/ModuleContext.h"
        "${CMAKE_SOURCE_DIR}/modules/host/module-runtime/AppPaths.cpp"
        "${CMAKE_SOURCE_DIR}/modules/host/module-runtime/AppPaths.h"
        "${CMAKE_SOURCE_DIR}/modules/host/module-runtime/PluginCatalog.cpp"
        "${CMAKE_SOURCE_DIR}/modules/host/module-runtime/PluginCatalog.h"
        "${CMAKE_SOURCE_DIR}/modules/host/module-runtime/SettingsService.cpp"
        "${CMAKE_SOURCE_DIR}/modules/host/module-runtime/SettingsService.h"
        "${CMAKE_SOURCE_DIR}/modules/host/module-runtime/PluginLoader.cpp"
        "${CMAKE_SOURCE_DIR}/modules/host/module-runtime/PluginLoader.h"
        "${CMAKE_SOURCE_DIR}/modules/host/module-runtime/ModuleRegistry.h"
        "${CMAKE_SOURCE_DIR}/modules/host/module-runtime/PluginResolver.cpp"
        "${CMAKE_SOURCE_DIR}/modules/host/module-runtime/PluginResolver.h"
        "${CMAKE_SOURCE_DIR}/modules/host/module-runtime/PluginMetadata.cpp"
        "${CMAKE_SOURCE_DIR}/modules/host/module-runtime/PluginMetadata.h"
        "${CMAKE_SOURCE_DIR}/modules/host/module-runtime/ModuleSettingsImpl.h"
        "${CMAKE_SOURCE_DIR}/modules/host/module-runtime/Services.h"
)

set(_FRAMELIFT_HOST_SETTINGS_SOURCES
        "${CMAKE_SOURCE_DIR}/modules/host/settings/CoreSettings.h"
        "${CMAKE_SOURCE_DIR}/modules/host/settings/Settings.cpp"
        "${CMAKE_SOURCE_DIR}/modules/host/settings/Settings.h"
        "${CMAKE_SOURCE_DIR}/modules/host/settings/SettingsMapping.h"
        "${CMAKE_SOURCE_DIR}/modules/host/settings/SettingsRegistry.h"
)

set(_FRAMELIFT_HOST_SERVICES_SOURCES
        "${CMAKE_SOURCE_DIR}/modules/host/services/FileDialogServiceImpl.cpp"
        "${CMAKE_SOURCE_DIR}/modules/host/services/FileDialogServiceImpl.h"
        "${CMAKE_SOURCE_DIR}/modules/host/services/HotkeysImpl.cpp"
        "${CMAKE_SOURCE_DIR}/modules/host/services/HotkeysImpl.h"
        "${CMAKE_SOURCE_DIR}/modules/host/services/JsonServiceImpl.cpp"
        "${CMAKE_SOURCE_DIR}/modules/host/services/JsonServiceImpl.h"
)

set(_FRAMELIFT_HOST_CONTROLS_SOURCES
        "${CMAKE_SOURCE_DIR}/modules/host/controls/CoreCommands.cpp"
        "${CMAKE_SOURCE_DIR}/modules/host/controls/CoreCommands.h"
        "${CMAKE_SOURCE_DIR}/modules/host/controls/PlaybackControls.cpp"
        "${CMAKE_SOURCE_DIR}/modules/host/controls/PlaybackControls.h"
)

set(_FRAMELIFT_HOST_LOGGING_SOURCES
        "${CMAKE_SOURCE_DIR}/modules/host/logging/Log.cpp"
        "${CMAKE_SOURCE_DIR}/modules/host/logging/LogBuffer.cpp"
        "${CMAKE_SOURCE_DIR}/modules/host/logging/LogBuffer.h"
)

set(_FRAMELIFT_HOST_AUDIO_SOURCES
        "${CMAKE_SOURCE_DIR}/modules/host/audio/AudioSettings.h"
        "${CMAKE_SOURCE_DIR}/modules/host/audio/FFmpegAudioFilter.cpp"
        "${CMAKE_SOURCE_DIR}/modules/host/audio/FFmpegAudioFilter.h"
        "${CMAKE_SOURCE_DIR}/modules/host/audio/FFmpegAudioOptions.h"
        "${CMAKE_SOURCE_DIR}/modules/host/audio/FFmpegAudioOutput.cpp"
        "${CMAKE_SOURCE_DIR}/modules/host/audio/FFmpegAudioOutput.h"
)

set(_FRAMELIFT_HOST_READ_AHEAD_SOURCES
        "${CMAKE_SOURCE_DIR}/modules/host/read-ahead/CacheSettings.h"
        "${CMAKE_SOURCE_DIR}/modules/host/read-ahead/ReadAheadCache.h"
)

# Host UI settings/theme helpers consumed by Qt settings code.
set(_FRAMELIFT_HOST_UI_SOURCES
        "${CMAKE_SOURCE_DIR}/modules/host/ui/ThemeSettings.h"
        "${CMAKE_SOURCE_DIR}/modules/host/ui/ThemeUtil.h"
        "${CMAKE_SOURCE_DIR}/modules/host/ui/UISettings.h"
)

set(_FRAMELIFT_HOST_WINDOW_SOURCES
        "${CMAKE_SOURCE_DIR}/modules/platform/window-qt/GraphicsInfoService.cpp"
        "${CMAKE_SOURCE_DIR}/modules/platform/window-qt/GraphicsInfoService.h"
        "${CMAKE_SOURCE_DIR}/modules/platform/window-qt/QtAppWindow.cpp"
        "${CMAKE_SOURCE_DIR}/modules/platform/window-qt/QtAppWindow.h"
        "${CMAKE_SOURCE_DIR}/modules/platform/window-qt/QmlCompositor.cpp"
        "${CMAKE_SOURCE_DIR}/modules/platform/window-qt/QmlCompositor.h"
        "${CMAKE_SOURCE_DIR}/modules/platform/window-qt/VideoItem.cpp"
        "${CMAKE_SOURCE_DIR}/modules/platform/window-qt/VideoItem.h"
        "${CMAKE_SOURCE_DIR}/modules/platform/window-qt/VideoRenderNode.cpp"
        "${CMAKE_SOURCE_DIR}/modules/platform/window-qt/VideoRenderNode.h"
        "${CMAKE_SOURCE_DIR}/modules/platform/window-qt/util.h"
)

set(_FRAMELIFT_HOST_WIN_SHELL_SOURCES
        "${CMAKE_SOURCE_DIR}/modules/platform/win-shell/WinShell.cpp"
        "${CMAKE_SOURCE_DIR}/modules/platform/win-shell/WinShell.h"
        "${CMAKE_SOURCE_DIR}/modules/platform/win-shell/ToastNotifier.cpp"
        "${CMAKE_SOURCE_DIR}/modules/platform/win-shell/ToastNotifier.h"
)

set(_FRAMELIFT_HOST_PLAYBACK_SOURCES
        "${CMAKE_SOURCE_DIR}/modules/host/playback/FFmpegClock.h"
        "${CMAKE_SOURCE_DIR}/modules/host/playback/FFmpegFilters.h"
        "${CMAKE_SOURCE_DIR}/modules/host/playback/FFmpegHwDecode.cpp"
        "${CMAKE_SOURCE_DIR}/modules/host/playback/FFmpegHwDecode.h"
        "${CMAKE_SOURCE_DIR}/modules/host/playback/FFmpegLetterbox.h"
        "${CMAKE_SOURCE_DIR}/modules/host/playback/FFmpegPacketQueue.h"
        "${CMAKE_SOURCE_DIR}/modules/host/playback/FFmpegPlayer.cpp"
        "${CMAKE_SOURCE_DIR}/modules/host/playback/FFmpegPlayer.h"
        "${CMAKE_SOURCE_DIR}/modules/host/playback/FFmpegPlayerCommands.cpp"
        "${CMAKE_SOURCE_DIR}/modules/host/playback/FFmpegPlayerInternal.h"
        "${CMAKE_SOURCE_DIR}/modules/host/playback/FFmpegPlayerProperties.cpp"
        "${CMAKE_SOURCE_DIR}/modules/host/playback/FFmpegPlayerRender.cpp"
        "${CMAKE_SOURCE_DIR}/modules/host/playback/FFmpegPlayerSession.cpp"
        "${CMAKE_SOURCE_DIR}/modules/host/playback/FFmpegPlayerTracks.cpp"
        "${CMAKE_SOURCE_DIR}/modules/host/playback/FFmpegPlayerWorkers.cpp"
        "${CMAKE_SOURCE_DIR}/modules/host/playback/FFmpegSettingsMapping.h"
        "${CMAKE_SOURCE_DIR}/modules/host/playback/FFmpegSidecarScan.h"
        "${CMAKE_SOURCE_DIR}/modules/host/playback/FFmpegSubtitleBlend.h"
        "${CMAKE_SOURCE_DIR}/modules/host/playback/FFmpegSubtitles.cpp"
        "${CMAKE_SOURCE_DIR}/modules/host/playback/FFmpegSubtitles.h"
        "${CMAKE_SOURCE_DIR}/modules/host/playback/FFmpegTrackLabel.h"
        "${CMAKE_SOURCE_DIR}/modules/host/playback/FFmpegTrackSelect.h"
        "${CMAKE_SOURCE_DIR}/modules/host/playback/PlaybackSettings.h"
        "${CMAKE_SOURCE_DIR}/modules/host/playback/PlayerEventSink.h"
        "${CMAKE_SOURCE_DIR}/modules/host/playback/SubtitleSettings.h"
        "${CMAKE_SOURCE_DIR}/modules/host/playback/VideoDecodeMode.h"
        "${CMAKE_SOURCE_DIR}/modules/host/playback/VideoFrameGate.h"
)

set(_FRAMELIFT_HOST_PLAYBACK_VULKAN_SOURCES
        "${CMAKE_SOURCE_DIR}/modules/host/playback/FFmpegVulkanDevice.cpp"
        "${CMAKE_SOURCE_DIR}/modules/host/playback/FFmpegVulkanDevice.h"
)

set(_FRAMELIFT_HOST_GRAPHICS_SOURCES
        "${CMAKE_SOURCE_DIR}/modules/gfx/graphics-core/GraphicsApi.h"
        "${CMAKE_SOURCE_DIR}/modules/gfx/graphics-core/GraphicsBackendFactory.cpp"
        "${CMAKE_SOURCE_DIR}/modules/gfx/graphics-core/IGraphicsBackend.h"
        "${CMAKE_SOURCE_DIR}/modules/gfx/graphics-core/IVideoRenderer.h"
)

set(_FRAMELIFT_HOST_GRAPHICS_OPENGL_SOURCES
        "${CMAKE_SOURCE_DIR}/modules/gfx/opengl/GlGraphicsBackend.cpp"
        "${CMAKE_SOURCE_DIR}/modules/gfx/opengl/GlGraphicsBackend.h"
        "${CMAKE_SOURCE_DIR}/modules/gfx/opengl/GlVideoRenderer.cpp"
        "${CMAKE_SOURCE_DIR}/modules/gfx/opengl/GlVideoRenderer.h"
)

set(_FRAMELIFT_HOST_GRAPHICS_VULKAN_SOURCES
        "${CMAKE_SOURCE_DIR}/modules/gfx/vulkan/VulkanDeviceInfo.h"
        "${CMAKE_SOURCE_DIR}/modules/gfx/vulkan/VulkanGraphicsBackend.cpp"
        "${CMAKE_SOURCE_DIR}/modules/gfx/vulkan/VulkanGraphicsBackend.h"
        "${CMAKE_SOURCE_DIR}/modules/gfx/vulkan/VulkanQueueLock.h"
        "${CMAKE_SOURCE_DIR}/modules/gfx/vulkan/VulkanVideoRenderer.cpp"
        "${CMAKE_SOURCE_DIR}/modules/gfx/vulkan/VulkanVideoRenderer.h"
)

# ── Built-in host modules as OBJECT libraries ─────────────────────────────────
# Each module with compilable sources becomes an OBJECT library the host exe links,
# giving every module its own target (per-module flags, IDE grouping, clear ownership).
# Header-only modules (read-ahead, ui) have no TU to compile, so the exe carries their
# headers for IDE visibility and they resolve via the shared include dirs. The host
# modules are tightly coupled at the header level (settings/playback/audio/gfx mutually
# include), so every module object lib gets the same shared host include set rather than
# a per-module include graph — see FrameLiftHostIncludes.cmake.

# Compiled by the host exe itself: the app entry point and the header-only modules.
set(FRAMELIFT_HOST_CORE_SOURCES ${_FRAMELIFT_HOST_CORE_SOURCES})
set(FRAMELIFT_HOST_HEADER_ONLY_SOURCES
        ${_FRAMELIFT_HOST_READ_AHEAD_SOURCES}
        ${_FRAMELIFT_HOST_UI_SOURCES})

set_property(GLOBAL PROPERTY FRAMELIFT_HOST_MODULE_TARGETS)

# framelift_add_host_module(<target> <sources...>) — an OBJECT library built with the
# host's C++ standard, include dirs, module defines, Qt, and Release optimizations.
# PRIVATE link items only supply compile usage requirements (include dirs / defines);
# the real libraries are linked by the host exe. Extra per-module link deps (Vulkan, …)
# are added by the caller afterwards.
function(framelift_add_host_module target)
    add_library(${target} OBJECT ${ARGN})
    set_target_properties(${target} PROPERTIES
            CXX_STANDARD 23
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS OFF
            # Host modules carry Q_OBJECT types (window-qt, …); moc them. A no-op on
            # modules that have none.
            AUTOMOC ON)
    target_include_directories(${target} PRIVATE
            "${CMAKE_SOURCE_DIR}/src"
            "${CMAKE_BINARY_DIR}"  # generated Version.h
            ${FRAMELIFT_GRAPHICS_INCLUDE_DIRS}
            ${FRAMELIFT_HOST_MODULE_INCLUDE_DIRS})
    target_link_libraries(${target} PRIVATE
            FrameLiftSdk OpenGL::GL ffmpeg libass
            Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Multimedia Qt6::Widgets)
    framelift_apply_builtin_module_definitions(${target})
    framelift_apply_release_opts(${target})
    set_property(GLOBAL APPEND PROPERTY FRAMELIFT_HOST_MODULE_TARGETS ${target})
    # Group only in-tree files in the IDE (generated SPIR-V headers live under the build dir).
    set(_sg ${ARGN})
    list(FILTER _sg EXCLUDE REGEX "^${CMAKE_BINARY_DIR}")
    source_group(TREE "${CMAKE_SOURCE_DIR}" FILES ${_sg})
endfunction()

framelift_add_host_module(framelift_mod_logging        ${_FRAMELIFT_HOST_LOGGING_SOURCES})
framelift_add_host_module(framelift_mod_services       ${_FRAMELIFT_HOST_SERVICES_SOURCES})
framelift_add_host_module(framelift_mod_settings       ${_FRAMELIFT_HOST_SETTINGS_SOURCES})
framelift_add_host_module(framelift_mod_module_runtime ${_FRAMELIFT_HOST_MODULE_RUNTIME_SOURCES})
framelift_add_host_module(framelift_mod_controls       ${_FRAMELIFT_HOST_CONTROLS_SOURCES})
framelift_add_host_module(framelift_mod_audio          ${_FRAMELIFT_HOST_AUDIO_SOURCES})
framelift_add_host_module(framelift_mod_window_qt      ${_FRAMELIFT_HOST_WINDOW_SOURCES})
framelift_add_host_module(framelift_mod_graphics_core  ${_FRAMELIFT_HOST_GRAPHICS_SOURCES})
framelift_add_host_module(framelift_mod_graphics_opengl ${_FRAMELIFT_HOST_GRAPHICS_OPENGL_SOURCES})

# Playback (+ its Vulkan hw-decode device when the Vulkan backend is enabled).
set(_framelift_playback_sources ${_FRAMELIFT_HOST_PLAYBACK_SOURCES})
if (FRAMELIFT_MODULE_GRAPHICS_VULKAN)
    list(APPEND _framelift_playback_sources ${_FRAMELIFT_HOST_PLAYBACK_VULKAN_SOURCES})
endif ()
framelift_add_host_module(framelift_mod_playback ${_framelift_playback_sources})
if (WIN32)
    # winmm: timeBeginPeriod/timeEndPeriod used by the video frame-pacing timer in
    # FFmpegPlayerWorkers.cpp (this module is where those calls actually live).
    target_link_libraries(framelift_mod_playback PRIVATE winmm)
endif ()
if (FRAMELIFT_MODULE_GRAPHICS_VULKAN)
    target_link_libraries(framelift_mod_playback PRIVATE
            Vulkan::Vulkan Vulkan::Headers GPUOpen::VulkanMemoryAllocator)
endif ()

# Vulkan graphics backend + renderer; compiles the SPIR-V shader headers it #includes.
if (FRAMELIFT_MODULE_GRAPHICS_VULKAN)
    framelift_compile_shader("${CMAKE_SOURCE_DIR}/modules/gfx/vulkan/shaders/video.vert" kVideoVertSpv FL_VIDEO_VERT_SPV)
    framelift_compile_shader("${CMAKE_SOURCE_DIR}/modules/gfx/vulkan/shaders/video.frag" kVideoFragSpv FL_VIDEO_FRAG_SPV)
    framelift_add_host_module(framelift_mod_graphics_vulkan
            ${_FRAMELIFT_HOST_GRAPHICS_VULKAN_SOURCES} "${FL_VIDEO_VERT_SPV}" "${FL_VIDEO_FRAG_SPV}")
    target_include_directories(framelift_mod_graphics_vulkan PRIVATE "${FRAMELIFT_SHADER_GEN_DIR}")
    target_link_libraries(framelift_mod_graphics_vulkan PRIVATE
            Vulkan::Vulkan Vulkan::Headers GPUOpen::VulkanMemoryAllocator)
endif ()

if (FRAMELIFT_MODULE_WIN_SHELL)
    framelift_add_host_module(framelift_mod_win_shell ${_FRAMELIFT_HOST_WIN_SHELL_SOURCES})
endif ()
