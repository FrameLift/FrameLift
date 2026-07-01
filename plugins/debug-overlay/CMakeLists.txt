add_framelift_plugin(DebugOverlay
        PLUGIN_JSON "${CMAKE_CURRENT_SOURCE_DIR}/DebugOverlay.Plugin.json"
        QML_URI FrameLift.Plugins.DebugOverlay
        QML_ENTRY "${CMAKE_CURRENT_SOURCE_DIR}/DebugOverlay.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/DebugOverlay.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/DebugOverlay.h"
        ${FRAMELIFT_SDK_SOURCES}
)
