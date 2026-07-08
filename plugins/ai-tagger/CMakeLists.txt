# AI Tagger plugin — links a prebuilt llama.cpp (+ its mtmd vision helper) for local
# inference. The host and SDK stay dependency-free; only this plugin sees llama/mtmd.
#
# We link the system/prebuilt llama.cpp rather than building it from source: it exposes
# a `llama` CMake package (which pulls in ggml) plus a libmtmd shared library. If those
# aren't installed, the plugin is skipped (its features are optional) so the rest of the
# build is unaffected.

find_package(llama CONFIG QUIET)
find_library(MTMD_LIBRARY NAMES mtmd)
find_path(MTMD_INCLUDE_DIR NAMES mtmd.h)

if (NOT llama_FOUND OR NOT MTMD_LIBRARY OR NOT MTMD_INCLUDE_DIR)
    message(STATUS "AITagger: prebuilt llama.cpp + mtmd not found; skipping the AI Tagger plugin. "
            "Install llama.cpp (providing the 'llama' CMake package and libmtmd) to enable it.")
    return()
endif ()

add_framelift_plugin(AITagger
        PLUGIN_JSON "${CMAKE_CURRENT_SOURCE_DIR}/AITagger.Plugin.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/AITagger.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/AITagger.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/InferenceBackend.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/LlamaBackend.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/SampleScheduler.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/SampleScheduler.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/TagStore.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/TagStore.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/TagTypes.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/TagWorker.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/TagWorker.h"
        ${FRAMELIFT_SDK_SOURCES}
)

target_link_libraries(AITagger PRIVATE llama ${MTMD_LIBRARY})
target_include_directories(AITagger PRIVATE "${MTMD_INCLUDE_DIR}")
