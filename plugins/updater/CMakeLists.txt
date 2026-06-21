add_framelift_plugin(Updater
        PLUGIN_JSON "${CMAKE_CURRENT_SOURCE_DIR}/Updater.Plugin.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/core/Updater.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/core/Updater.h"
        ${FRAMELIFT_SDK_SOURCES}
)

get_target_property(_updater_disabled Updater FRAMELIFT_PLUGIN_DISABLED)
if (NOT _updater_disabled)
    target_include_directories(Updater PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/core")
    # Updater parses GitHub release JSON — bring its own json dependency.
    target_link_libraries(Updater PRIVATE winhttp nlohmann_json::nlohmann_json)
endif ()
