# Skeleton build root for a FrameLift plugin.
#
# This file sits at the root of the extracted framelift-sdk-<ver>.zip. It locates the
# SDK shipped alongside it; add your plugin below. For a complete, buildable worked
# example, see the FrameLift-Examples repository:
#   https://github.com/FrameLift/FrameLift-Examples
cmake_minimum_required(VERSION 3.28)
project(MyFrameLiftPlugin LANGUAGES CXX)

find_package(FrameLiftSdk REQUIRED PATHS "${CMAKE_CURRENT_SOURCE_DIR}/cmake" NO_DEFAULT_PATH)

# Declare your plugin. Ship a .Plugin.json alongside your sources;
# identity/version/ABI/dependencies come from that metadata, not from CMake.
#
# add_framelift_plugin(MyPlugin
#         PLUGIN_JSON "${CMAKE_CURRENT_SOURCE_DIR}/MyPlugin.Plugin.json"
#         MyPlugin.cpp
#         ${FRAMELIFT_SDK_SOURCES})
