add_framelift_plugin(DebugOverlay
    PLUGIN_JSON "${CMAKE_CURRENT_SOURCE_DIR}/DebugOverlay.Plugin.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/core/DebugOverlay.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/core/DebugOverlay.h"
    ${FRAMELIFT_SDK_SOURCES}
)
target_include_directories(DebugOverlay PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/core")
