add_framelift_plugin(DBViewer
        PLUGIN_JSON "${CMAKE_CURRENT_SOURCE_DIR}/DBViewer.Plugin.json"
        QML_URI FrameLift.Plugins.DBViewer
        QML_ENTRY "${CMAKE_CURRENT_SOURCE_DIR}/DBViewer.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/DBViewer.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/DBViewer.h"
        ${FRAMELIFT_SDK_SOURCES}
)
