add_framelift_plugin(Playlist
        PLUGIN_JSON "${CMAKE_CURRENT_SOURCE_DIR}/Playlist.Plugin.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/core/Playlist.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/core/Playlist.h"
        ${FRAMELIFT_SDK_SOURCES}
)
target_include_directories(Playlist PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/core")
target_link_libraries(Playlist PRIVATE SDL3::SDL3)
