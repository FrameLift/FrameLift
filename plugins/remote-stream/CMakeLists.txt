add_framelift_plugin(RemoteStream
        PLUGIN_JSON "${CMAKE_CURRENT_SOURCE_DIR}/RemoteStream.Plugin.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/core/RemoteStream.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/core/RemoteStream.h"
        ${FRAMELIFT_SDK_SOURCES}
)
target_include_directories(RemoteStream PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/core")
