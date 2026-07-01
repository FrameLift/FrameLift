add_framelift_plugin(Benchmark
        PLUGIN_JSON "${CMAKE_CURRENT_SOURCE_DIR}/Benchmark.Plugin.json"
        QML_URI FrameLift.Plugins.Benchmark
        QML_ENTRY "${CMAKE_CURRENT_SOURCE_DIR}/Benchmark.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/Benchmark.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Benchmark.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/BenchmarkSettings.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/BenchmarkSettings.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/SysStats.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/SysStats.h"
        ${FRAMELIFT_SDK_SOURCES}
        QML_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/BenchmarkSettings.qml"
)
if (WIN32)
    # psapi → GetProcessMemoryInfo; pdh → GPU Engine performance counters.
    target_link_libraries(Benchmark PRIVATE psapi pdh)
endif ()
