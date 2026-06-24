add_framelift_plugin(ContextMenu
        PLUGIN_JSON "${CMAKE_CURRENT_SOURCE_DIR}/ContextMenu.Plugin.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/core/ContextMenuModule.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/core/ContextMenuModule.h"
        ${FRAMELIFT_SDK_SOURCES}
)
target_include_directories(ContextMenu PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/core")
