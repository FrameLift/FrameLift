add_framelift_plugin(LogViewer
    PLUGIN_JSON "${CMAKE_CURRENT_SOURCE_DIR}/LogViewer.Plugin.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/core/LogViewer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/core/LogViewer.h"
    ${FRAMELIFT_SDK_SOURCES}
)
target_include_directories(LogViewer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/core")
