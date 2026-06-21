add_framelift_plugin(History
        PLUGIN_JSON "${CMAKE_CURRENT_SOURCE_DIR}/History.Plugin.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/core/History.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/core/History.h"
        ${FRAMELIFT_SDK_SOURCES}
)
target_include_directories(History PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/core")
# History persists its entries as JSON — it brings its own json dependency
# (the SDK itself is dependency-free).
target_link_libraries(History PRIVATE nlohmann_json::nlohmann_json)
