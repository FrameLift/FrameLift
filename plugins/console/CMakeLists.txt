add_framelift_plugin(Console
        PLUGIN_JSON "${CMAKE_CURRENT_SOURCE_DIR}/Console.Plugin.json"
        QML_URI FrameLift.Plugins.Console
        QML_ENTRY "${CMAKE_CURRENT_SOURCE_DIR}/Console.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/Console.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Console.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/ConsoleSettings.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/ConsoleSettings.h"
        ${FRAMELIFT_SDK_SOURCES}
        QML_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/ConsoleSettings.qml"
)
