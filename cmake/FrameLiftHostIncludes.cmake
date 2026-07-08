# Host/gfx internal include directories.
#
# Single source of truth shared by the host build (root CMakeLists) and the test build
# (tests/, which compiles selected host sources directly). Resolved against
# FRAMELIFT_SOURCE_ROOT so it works both in-tree (== CMAKE_SOURCE_DIR) and in the
# standalone tests build, where CMAKE_SOURCE_DIR is the tests/ dir.

if (NOT DEFINED FRAMELIFT_SOURCE_ROOT)
    set(FRAMELIFT_SOURCE_ROOT "${CMAKE_SOURCE_DIR}")
endif ()

set(FRAMELIFT_GRAPHICS_INCLUDE_DIRS
        "${FRAMELIFT_SOURCE_ROOT}/modules/gfx/graphics-core"
        "${FRAMELIFT_SOURCE_ROOT}/modules/gfx/opengl"
        "${FRAMELIFT_SOURCE_ROOT}/modules/gfx/vulkan"
)
set(FRAMELIFT_HOST_MODULE_INCLUDE_DIRS
        "${FRAMELIFT_SOURCE_ROOT}/modules/host/module-runtime"
        "${FRAMELIFT_SOURCE_ROOT}/modules/host/settings"
        "${FRAMELIFT_SOURCE_ROOT}/modules/host/services"
        "${FRAMELIFT_SOURCE_ROOT}/modules/host/controls"
        "${FRAMELIFT_SOURCE_ROOT}/modules/host/logging"
        "${FRAMELIFT_SOURCE_ROOT}/modules/host/audio"
        "${FRAMELIFT_SOURCE_ROOT}/modules/host/playback"
        "${FRAMELIFT_SOURCE_ROOT}/modules/host/frame-sampler"
        "${FRAMELIFT_SOURCE_ROOT}/modules/host/read-ahead"
        "${FRAMELIFT_SOURCE_ROOT}/modules/host/ui"
        "${FRAMELIFT_SOURCE_ROOT}/modules/platform/window-qt"
        "${FRAMELIFT_SOURCE_ROOT}/modules/platform/win-shell"
)
