add_framelift_plugin(ContextMenu
        PLUGIN_JSON "${CMAKE_CURRENT_SOURCE_DIR}/ContextMenu.Plugin.json"
        QML_URI FrameLift.Plugins.ContextMenu
        QML_ENTRY "${CMAKE_CURRENT_SOURCE_DIR}/ContextMenu.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/ContextMenuModule.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/ContextMenuModule.h"
        ${FRAMELIFT_SDK_SOURCES}
        QML_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/ContextMenuContent.qml"
)
