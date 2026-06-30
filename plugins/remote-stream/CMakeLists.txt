add_framelift_plugin(RemoteStream
        PLUGIN_JSON "${CMAKE_CURRENT_SOURCE_DIR}/RemoteStream.Plugin.json"
        QML_URI FrameLift.Plugins.RemoteStream
        QML_ENTRY "${CMAKE_CURRENT_SOURCE_DIR}/RemoteStream.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/RemoteStream.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/RemoteStream.h"
        ${FRAMELIFT_SDK_SOURCES}
)
