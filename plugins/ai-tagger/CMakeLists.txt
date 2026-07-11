if (NOT FRAMELIFT_MODULE_AI)
    message(STATUS "AITagger: shared AI host module disabled; skipping the AI Tagger plugin.")
    return()
endif ()

add_framelift_plugin(AITagger
        PLUGIN_JSON "${CMAKE_CURRENT_SOURCE_DIR}/AITagger.Plugin.json"
        QML_URI FrameLift.Plugins.AITagger
        QML_ENTRY "${CMAKE_CURRENT_SOURCE_DIR}/AITagger.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/AITagger.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/AITagger.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/AITaggerSettings.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/AITaggerSettings.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/InferenceBackend.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/HostAIBackend.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/HostAIBackend.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/SampleScheduler.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/SampleScheduler.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/TagStore.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/TagStore.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/TagTypes.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/TagWorker.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/TagWorker.h"
        ${FRAMELIFT_SDK_SOURCES}
        QML_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/AITaggerSettings.qml"
)
