add_framelift_plugin(LogViewer
        PLUGIN_JSON "${CMAKE_CURRENT_SOURCE_DIR}/LogViewer.Plugin.json"
        QML_URI FrameLift.Plugins.LogViewer
        QML_ENTRY "${CMAKE_CURRENT_SOURCE_DIR}/LogViewer.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/LogViewer.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/LogViewer.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/LogViewerSettings.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/LogViewerSettings.h"
        ${FRAMELIFT_SDK_SOURCES}
        QML_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/LogViewerSettings.qml"
)
