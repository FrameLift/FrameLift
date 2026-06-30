add_framelift_plugin(Overlay
        PLUGIN_JSON "${CMAKE_CURRENT_SOURCE_DIR}/Overlay.Plugin.json"
        QML_URI FrameLift.Plugins.Overlay
        QML_ENTRY "${CMAKE_CURRENT_SOURCE_DIR}/Overlay.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/Overlay.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Overlay.h"
        ${FRAMELIFT_SDK_SOURCES}
)
set_source_files_properties("${CMAKE_SOURCE_DIR}/assets/img/icon.svg" PROPERTIES QT_RESOURCE_ALIAS icon.svg)
qt_add_resources(
        Overlay overlay_assets
        PREFIX "/qt/qml/FrameLift/Plugins/Overlay"
        FILES "${CMAKE_SOURCE_DIR}/assets/img/icon.svg")
