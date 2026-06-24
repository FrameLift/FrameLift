# FrameLift plugin SDK CMake module — the single source of truth for building plugins.
#
# Two modes, selected by FRAMELIFT_SDK_STANDALONE:
#   • In-tree (default): included by the root CMakeLists while building FrameLift.
#     FrameLiftSdk points at sdk/include + the generated-header dir; packages land next
#     to the host exe in <FrameLift>/packages.
#   • Standalone: included by FrameLiftSdkConfig.cmake from an installed SDK package.
#     FrameLiftSdk points at the packaged include/ + src/; no host target exists, so
#     packages land in <build>/packages.
#
# The public SDK target (FrameLiftSdk) is dependency-clean: it exposes only the SDK
# include path. No imgui/spdlog/stb/json — the host owns those.

if (NOT DEFINED FRAMELIFT_SDK_STANDALONE)
    set(FRAMELIFT_SDK_STANDALONE OFF)
endif ()

if (FRAMELIFT_SDK_STANDALONE)
    # Installed layout: <prefix>/cmake/FrameLiftSdk.cmake, <prefix>/include, <prefix>/src
    get_filename_component(_framelift_sdk_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
    set(_framelift_sdk_include "${_framelift_sdk_root}/include")
    set(_framelift_sdk_src "${_framelift_sdk_root}/src")
else ()
    set(_framelift_sdk_include "${CMAKE_SOURCE_DIR}/sdk/include")
    set(_framelift_sdk_src "${CMAKE_SOURCE_DIR}/sdk/src")
endif ()

include("${CMAKE_CURRENT_LIST_DIR}/FrameLiftPackageMetadata.cmake")

# ── Public plugin SDK (headers only) ──────────────────────────────────────────
if (NOT TARGET FrameLiftSdk)
    add_library(FrameLiftSdk INTERFACE)
    target_include_directories(FrameLiftSdk INTERFACE "${_framelift_sdk_include}")
    if (NOT FRAMELIFT_SDK_STANDALONE)
        # In-tree plugins (Overlay, Updater) consume the generated Version.h.
        target_include_directories(FrameLiftSdk INTERFACE "${CMAKE_BINARY_DIR}")
    endif ()
endif ()

# ── SDK helper sources compiled into every plugin (and, in-tree, the host) ────
# MinGW static-runtime plugins need their own copy of each translation unit, so
# these are compiled in rather than shipped as a shared library.
set(FRAMELIFT_SDK_SOURCES
        "${_framelift_sdk_src}/ModuleBase.cpp"
        "${_framelift_sdk_src}/Log.cpp"
        "${_framelift_sdk_src}/Panel.cpp"
        "${_framelift_sdk_src}/Widgets.cpp"
        "${_framelift_sdk_src}/Window.cpp"
        "${_framelift_sdk_src}/KeyNames.cpp"
)

# ── Helper: add_framelift_plugin(NAME PLUGIN_JSON <file> src1 src2 ...) ─────────────
# Creates a SHARED plugin library that links the SDK and outputs into packages/.
function(add_framelift_plugin NAME)
    cmake_parse_arguments(_FL_PLUGIN "" "PLUGIN_JSON" "" ${ARGN})
    if (NOT _FL_PLUGIN_PLUGIN_JSON)
        message(FATAL_ERROR "add_framelift_plugin(${NAME}) requires PLUGIN_JSON <file>")
    endif ()

    framelift_generate_package_metadata(
            "${NAME}"
            "${_FL_PLUGIN_PLUGIN_JSON}"
            _framelift_metadata_header
            _framelift_plugin_enabled
            _framelift_package_id
            _framelift_module_binary_name)

    if (NOT _framelift_plugin_enabled)
        add_custom_target(${NAME})
        set_property(TARGET ${NAME} PROPERTY FRAMELIFT_PLUGIN_DISABLED TRUE)
        set_property(GLOBAL APPEND PROPERTY FRAMELIFT_PLUGIN_TARGETS ${NAME})
        return()
    endif ()

    get_filename_component(_framelift_metadata_header_name "${_framelift_metadata_header}" NAME)

    add_library(${NAME} SHARED ${_FL_PLUGIN_UNPARSED_ARGUMENTS})
    set_property(GLOBAL APPEND PROPERTY FRAMELIFT_PLUGIN_TARGETS ${NAME})
    set_target_properties(${NAME} PROPERTIES
            CXX_STANDARD 23
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS OFF
            # The host scans packages/ for shared libraries; MinGW would otherwise
            # emit lib<Name>.dll and the load would fail.
            OUTPUT_NAME "${_framelift_module_binary_name}"
            PREFIX "")
    target_compile_definitions(${NAME} PRIVATE
            FRAMELIFT_BUILDING_MODULE
            FRAMELIFT_MODULE_METADATA_HEADER="${_framelift_metadata_header_name}")
    target_include_directories(${NAME} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(${NAME} PRIVATE FrameLiftSdk)
    if (MINGW)
        target_link_options(${NAME} PRIVATE
                -static-libgcc -static-libstdc++ -static)
    endif ()
    if (TARGET FrameLift)
        set(_framelift_plugin_out "$<TARGET_FILE_DIR:FrameLift>/packages")
    else ()
        set(_framelift_plugin_out "${CMAKE_BINARY_DIR}/packages")
    endif ()
    # RUNTIME covers the Windows .dll; LIBRARY covers the Linux/macOS .so/.dylib
    # (a SHARED lib is a LIBRARY artifact off-Windows). Set both so packages always
    # land in packages/ regardless of platform.
    set_target_properties(${NAME} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${_framelift_plugin_out}"
            LIBRARY_OUTPUT_DIRECTORY "${_framelift_plugin_out}")
    # Ensure the packages/ output directory exists before linking.
    add_custom_command(TARGET ${NAME} PRE_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_framelift_plugin_out}")
endfunction()
