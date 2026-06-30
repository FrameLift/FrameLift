add_framelift_plugin(SettingsMenu
        PLUGIN_JSON "${CMAKE_CURRENT_SOURCE_DIR}/SettingsMenu.Plugin.json"
        QML_URI FrameLift.Plugins.SettingsMenu
        QML_ENTRY "${CMAKE_CURRENT_SOURCE_DIR}/SettingsMenu.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/SettingsMenu.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/SettingsMenu.h"
        "${CMAKE_SOURCE_DIR}/modules/host/settings/Settings.cpp"
        ${FRAMELIFT_SDK_SOURCES}
        QML_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/GenericSettingsPage.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/AudioSettings.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/CacheSettings.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/FilesSettings.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/KeybindSettings.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/PlaybackSettings.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/SubtitleSettings.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/ThemeSettings.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/UISettings.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/SettingsPluginsPage.qml"
)
# SettingsMenu compiles the host Settings.cpp directly, so it needs the host
# settings/src include paths (the plugin's own dir is added by add_framelift_plugin)
# and the built-in module defines its headers gate on (e.g. FRAMELIFT_MODULE_GRAPHICS_VULKAN
# in PlaybackSettings.h) so it compiles for the same configuration as the host.
target_include_directories(SettingsMenu PRIVATE
        "${CMAKE_SOURCE_DIR}/src"
        ${FRAMELIFT_HOST_MODULE_INCLUDE_DIRS}
)
framelift_apply_builtin_module_definitions(SettingsMenu)
