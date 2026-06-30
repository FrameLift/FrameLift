add_framelift_plugin(Playlist
        PLUGIN_JSON "${CMAKE_CURRENT_SOURCE_DIR}/Playlist.Plugin.json"
        QML_URI FrameLift.Plugins.Playlist
        QML_ENTRY "${CMAKE_CURRENT_SOURCE_DIR}/Playlist.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/Playlist.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Playlist.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/PlaylistSettings.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/PlaylistSettings.h"
        ${FRAMELIFT_SDK_SOURCES}
        QML_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/PlaylistSettings.qml"
)
