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
        "${CMAKE_SOURCE_DIR}/modules/host/module-runtime/PackageConfig.cpp"
        "${CMAKE_SOURCE_DIR}/modules/host/module-runtime/PackageConfig.h"
        "${CMAKE_SOURCE_DIR}/modules/host/module-runtime/ModuleContext.cpp"
        "${CMAKE_SOURCE_DIR}/modules/host/module-runtime/ModuleContext.h"
        "${CMAKE_SOURCE_DIR}/modules/host/module-runtime/PackageLoader.cpp"
        "${CMAKE_SOURCE_DIR}/modules/host/module-runtime/PackageLoader.h"
        "${CMAKE_SOURCE_DIR}/modules/host/module-runtime/ModuleRegistry.h"
        "${CMAKE_SOURCE_DIR}/modules/host/module-runtime/PackageResolver.cpp"
        "${CMAKE_SOURCE_DIR}/modules/host/module-runtime/PackageResolver.h"
        "${CMAKE_SOURCE_DIR}/modules/host/module-runtime/PackageMetadata.cpp"
        "${CMAKE_SOURCE_DIR}/modules/host/module-runtime/PackageMetadata.h"
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
        "${CMAKE_SOURCE_DIR}/modules/host/services/FocusManagerImpl.h"
        "${CMAKE_SOURCE_DIR}/modules/host/services/HotkeysImpl.cpp"
        "${CMAKE_SOURCE_DIR}/modules/host/services/HotkeysImpl.h"
        "${CMAKE_SOURCE_DIR}/modules/host/services/JsonServiceImpl.cpp"
        "${CMAKE_SOURCE_DIR}/modules/host/services/JsonServiceImpl.h"
)

set(_FRAMELIFT_HOST_CONTROLS_SOURCES
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

# The legacy Dear ImGui host UI translation units were removed during the Qt/QML
# migration. Keep only the ImGui-free settings/theme headers still consumed by
# Qt-era settings code.
set(_FRAMELIFT_HOST_UI_SOURCES
        "${CMAKE_SOURCE_DIR}/modules/host/ui/ThemeSettings.h"
        "${CMAKE_SOURCE_DIR}/modules/host/ui/ThemeUtil.h"
        "${CMAKE_SOURCE_DIR}/modules/host/ui/UiSettings.h"
)

set(_FRAMELIFT_HOST_WINDOW_SOURCES
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

set(_FRAMELIFT_HOST_WATCH_SOURCES
        "${CMAKE_SOURCE_DIR}/modules/platform/dir-watch/DirWatcher.cpp"
        "${CMAKE_SOURCE_DIR}/modules/platform/dir-watch/DirWatcher.h"
        "${CMAKE_SOURCE_DIR}/modules/platform/dir-watch/LinuxDirWatcher.cpp"
        "${CMAKE_SOURCE_DIR}/modules/platform/dir-watch/LinuxDirWatcher.h"
        "${CMAKE_SOURCE_DIR}/modules/platform/dir-watch/Win32DirWatcher.cpp"
        "${CMAKE_SOURCE_DIR}/modules/platform/dir-watch/Win32DirWatcher.h"
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
        "${CMAKE_SOURCE_DIR}/modules/host/playback/FFmpegSettingsMapping.h"
        "${CMAKE_SOURCE_DIR}/modules/host/playback/FFmpegSubtitleBlend.h"
        "${CMAKE_SOURCE_DIR}/modules/host/playback/FFmpegSubtitles.cpp"
        "${CMAKE_SOURCE_DIR}/modules/host/playback/FFmpegSubtitles.h"
        "${CMAKE_SOURCE_DIR}/modules/host/playback/FFmpegTrackLabel.h"
        "${CMAKE_SOURCE_DIR}/modules/host/playback/PlaybackSettings.h"
        "${CMAKE_SOURCE_DIR}/modules/host/playback/SubtitleSettings.h"
        "${CMAKE_SOURCE_DIR}/modules/host/playback/VideoDecodeMode.h"
)

set(_FRAMELIFT_HOST_PLAYBACK_VULKAN_SOURCES
        "${CMAKE_SOURCE_DIR}/modules/host/playback/FFmpegVulkanDevice.cpp"
        "${CMAKE_SOURCE_DIR}/modules/host/playback/FFmpegVulkanDevice.h"
)

set(_FRAMELIFT_HOST_GRAPHICS_SOURCES
        "${CMAKE_SOURCE_DIR}/modules/gfx/graphics-core/GraphicsApi.h"
        "${CMAKE_SOURCE_DIR}/modules/gfx/graphics-core/GraphicsBackendFactory.cpp"
        "${CMAKE_SOURCE_DIR}/modules/gfx/graphics-core/GraphicsSettings.h"
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

set(FRAMELIFT_HOST_SOURCES
        ${_FRAMELIFT_HOST_CORE_SOURCES}
        ${_FRAMELIFT_HOST_MODULE_RUNTIME_SOURCES}
        ${_FRAMELIFT_HOST_SETTINGS_SOURCES}
        ${_FRAMELIFT_HOST_SERVICES_SOURCES}
        ${_FRAMELIFT_HOST_CONTROLS_SOURCES}
        ${_FRAMELIFT_HOST_LOGGING_SOURCES}
        ${_FRAMELIFT_HOST_AUDIO_SOURCES}
        ${_FRAMELIFT_HOST_READ_AHEAD_SOURCES}
        ${_FRAMELIFT_HOST_UI_SOURCES}
        ${_FRAMELIFT_HOST_WINDOW_SOURCES}
        ${_FRAMELIFT_HOST_WATCH_SOURCES}
        ${_FRAMELIFT_HOST_PLAYBACK_SOURCES}
        ${_FRAMELIFT_HOST_GRAPHICS_SOURCES}
        ${_FRAMELIFT_HOST_GRAPHICS_OPENGL_SOURCES}
)

if (FRAMELIFT_MODULE_GRAPHICS_VULKAN)
    list(APPEND FRAMELIFT_HOST_SOURCES
            ${_FRAMELIFT_HOST_PLAYBACK_VULKAN_SOURCES}
            ${_FRAMELIFT_HOST_GRAPHICS_VULKAN_SOURCES}
    )
endif ()

if (FRAMELIFT_MODULE_WIN_SHELL)
    list(APPEND FRAMELIFT_HOST_SOURCES ${_FRAMELIFT_HOST_WIN_SHELL_SOURCES})
endif ()

source_group(TREE "${CMAKE_SOURCE_DIR}" FILES ${FRAMELIFT_HOST_SOURCES})
