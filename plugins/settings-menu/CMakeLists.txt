add_framelift_plugin(SettingsMenu
        PLUGIN_JSON "${CMAKE_CURRENT_SOURCE_DIR}/SettingsMenu.Plugin.json"
        QML_URI FrameLift.Plugins.SettingsMenu
        QML_ENTRY "${CMAKE_CURRENT_SOURCE_DIR}/SettingsMenu.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/SettingsMenu.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/SettingsMenu.h"
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
        "${CMAKE_CURRENT_SOURCE_DIR}/AdvancedSettings.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/AISettings.qml"
)
