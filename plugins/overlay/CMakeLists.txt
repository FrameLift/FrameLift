add_framelift_plugin(Overlay
        PLUGIN_JSON "${CMAKE_CURRENT_SOURCE_DIR}/Overlay.Plugin.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/core/Overlay.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/core/Overlay.h"
        ${FRAMELIFT_SDK_SOURCES}
)
target_include_directories(Overlay PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/core")
