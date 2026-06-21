add_framelift_plugin(History
        PLUGIN_JSON "${CMAKE_CURRENT_SOURCE_DIR}/History.Plugin.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/core/History.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/core/History.h"
        ${FRAMELIFT_SDK_SOURCES}
)
target_include_directories(History PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/core")
# History persists its entries as JSON via the host IJson service (ctx.GetService<IJson>())
# — no JSON library is linked into the plugin.
