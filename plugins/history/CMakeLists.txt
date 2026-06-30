add_framelift_plugin(History
        PLUGIN_JSON "${CMAKE_CURRENT_SOURCE_DIR}/History.Plugin.json"
        QML_URI FrameLift.Plugins.History
        QML_ENTRY "${CMAKE_CURRENT_SOURCE_DIR}/History.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/History.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/History.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/HistorySettings.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/HistorySettings.h"
        ${FRAMELIFT_SDK_SOURCES}
        QML_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/HistorySettings.qml"
)
