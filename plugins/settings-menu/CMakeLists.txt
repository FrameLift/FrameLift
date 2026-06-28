add_framelift_plugin(SettingsMenu
        PLUGIN_JSON "${CMAKE_CURRENT_SOURCE_DIR}/SettingsMenu.Plugin.json"
        QML_URI FrameLift.Plugins.SettingsMenu
        QML_ENTRY "${CMAKE_CURRENT_SOURCE_DIR}/SettingsMenu.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/SettingsMenu.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/SettingsMenu.h"
        "${CMAKE_SOURCE_DIR}/modules/host/settings/Settings.cpp"
        ${FRAMELIFT_SDK_SOURCES}
        QML_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/SettingRow.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/AudioSettings.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/CacheSettings.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/FilesSettings.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/KeybindSettings.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/PlaybackSettings.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/SubtitleSettings.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/ThemeSettings.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/UiSettings.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/SettingsPluginsPage.qml"
)
target_include_directories(SettingsMenu PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/src"
        ${FRAMELIFT_HOST_MODULE_INCLUDE_DIRS}
)
