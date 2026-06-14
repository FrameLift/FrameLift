add_framelift_plugin(RemoteStream
        "${CMAKE_CURRENT_SOURCE_DIR}/RemoteStream.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/RemoteStream.h"
        ${FRAMELIFT_SDK_SOURCES}
)
target_include_directories(RemoteStream PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
