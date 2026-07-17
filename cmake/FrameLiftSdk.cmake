# FrameLift plugin SDK CMake module — the single source of truth for building plugins.
#
# Two modes, selected by FRAMELIFT_SDK_STANDALONE:
#   • In-tree (default): included by the root CMakeLists while building FrameLift.
#     FrameLiftSdk points at sdk/include + the generated-header dir; plugins land next
#     to the host exe in <FrameLift>/plugins.
#   • Standalone: included by FrameLiftSdkConfig.cmake from an installed SDK package.
#     FrameLiftSdk points at the packaged include/ + src/; no host target exists, so
#     plugins land in <build>/plugins.
#
# The public SDK target exposes the SDK include path and Qt Core/QML/Quick. The host
# still owns FFmpeg and other implementation dependencies.

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

include("${CMAKE_CURRENT_LIST_DIR}/FrameLiftPluginMetadata.cmake")

# A plugin DLL/SO is a Qt plugin: its IPlugin factory carries Q_OBJECT/Q_PLUGIN_METADATA
# produced by FRAMELIFT_MODULE_ENTRY. AUTOMOC greps sources for
# moc-triggering tokens textually and cannot see those hidden behind our macros, so register
# the macro names — files using them then get moc'd (moc's own preprocessor expands the rest).
list(APPEND CMAKE_AUTOMOC_MACRO_NAMES "FRAMELIFT_MODULE_ENTRY")

# QPluginLoader + Q_PLUGIN_METADATA pull in QtCore; plugins build against Qt.
if (NOT TARGET Qt6::Quick)
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)
endif ()

# ── Public plugin SDK (headers + QtCore) ──────────────────────────────────────
if (NOT TARGET FrameLiftSdk)
    add_library(FrameLiftSdk INTERFACE)
    target_include_directories(FrameLiftSdk INTERFACE "${_framelift_sdk_include}")
    target_link_libraries(FrameLiftSdk INTERFACE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
    if (NOT FRAMELIFT_SDK_STANDALONE)
        # In-tree plugins (Overlay) consume the generated Version.h.
        target_include_directories(FrameLiftSdk INTERFACE "${CMAKE_BINARY_DIR}")
    endif ()
endif ()

# ── SDK helper sources compiled into every plugin (and, in-tree, the host) ────
# MinGW static-runtime plugins need their own copy of each translation unit, so
# these are compiled in rather than shipped as a shared library.
set(FRAMELIFT_SDK_SOURCES
        "${_framelift_sdk_src}/ModuleBase.cpp"
        "${_framelift_sdk_src}/Log.cpp"
        "${_framelift_sdk_src}/KeyNames.cpp"
)

# ── Helper: add_framelift_plugin(NAME PLUGIN_JSON <file>
#                                  QML_URI <uri> QML_ENTRY <file>
#                                  QML_FILES <files...> src1 src2 ...) ─────────
# Creates a SHARED plugin library that links the SDK and outputs into plugins/.
function(add_framelift_plugin NAME)
    cmake_parse_arguments(_FL_PLUGIN "" "PLUGIN_JSON;QML_URI;QML_ENTRY" "QML_FILES" ${ARGN})
    if (NOT _FL_PLUGIN_PLUGIN_JSON)
        message(FATAL_ERROR "add_framelift_plugin(${NAME}) requires PLUGIN_JSON <file>")
    endif ()

    # Production plugins may compile their own sources and the SDK helpers, but
    # never host implementation files. This guard applies only in-tree; a
    # standalone SDK package has no host source tree to cross into.
    if (NOT FRAMELIFT_SDK_STANDALONE)
        set(_framelift_host_src_root "${CMAKE_SOURCE_DIR}/src")
        set(_framelift_host_modules_root "${CMAKE_SOURCE_DIR}/modules")
        foreach (_framelift_plugin_source IN LISTS _FL_PLUGIN_UNPARSED_ARGUMENTS)
            get_filename_component(
                    _framelift_plugin_source_abs "${_framelift_plugin_source}" ABSOLUTE
                    BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
            cmake_path(IS_PREFIX _framelift_host_src_root "${_framelift_plugin_source_abs}" NORMALIZE _is_host_src)
            cmake_path(
                    IS_PREFIX _framelift_host_modules_root "${_framelift_plugin_source_abs}"
                    NORMALIZE _is_host_module)
            if (_is_host_src OR _is_host_module)
                message(FATAL_ERROR
                        "Plugin ${NAME} may not compile host source '${_framelift_plugin_source_abs}'")
            endif ()
        endforeach ()
    endif ()

    framelift_generate_plugin_metadata(
            "${NAME}"
            "${_FL_PLUGIN_PLUGIN_JSON}"
            _framelift_metadata_json
            _framelift_plugin_enabled
            _framelift_plugin_id
            _framelift_plugin_binary_name)

    if (NOT _framelift_plugin_enabled)
        add_custom_target(${NAME})
        set_property(TARGET ${NAME} PROPERTY FRAMELIFT_PLUGIN_DISABLED TRUE)
        set_property(GLOBAL APPEND PROPERTY FRAMELIFT_PLUGIN_TARGETS ${NAME})
        return()
    endif ()

    get_filename_component(_framelift_metadata_json_name "${_framelift_metadata_json}" NAME)

    add_library(${NAME} SHARED ${_FL_PLUGIN_UNPARSED_ARGUMENTS})
    set_property(GLOBAL APPEND PROPERTY FRAMELIFT_PLUGIN_TARGETS ${NAME})
    set_target_properties(${NAME} PROPERTIES
            CXX_STANDARD 23
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS OFF
            # The IPlugin factory is a Q_OBJECT plugin — moc it.
            AUTOMOC ON
            # The host scans plugins/ for shared libraries; MinGW would otherwise
            # emit lib<Name>.dll and the load would fail.
            OUTPUT_NAME "${_framelift_plugin_binary_name}"
            PREFIX "")
    target_compile_definitions(${NAME} PRIVATE
            FRAMELIFT_BUILDING_MODULE
            FRAMELIFT_PLUGIN_METADATA_JSON="${_framelift_metadata_json_name}"
            # Pulling in QtCore defines the unprefixed signals/slots/emit keyword macros,
            # which collide with ordinary plugin identifiers (e.g. a `slots` variable). The
            # generated IPlugin factory only uses the Q_* macros, so plugins keep their
            # identifiers and never touch the bare keywords.
            QT_NO_KEYWORDS)
    if (_FL_PLUGIN_QML_ENTRY)
        if (NOT _FL_PLUGIN_QML_URI)
            set(_FL_PLUGIN_QML_URI "FrameLift.Plugins.${NAME}")
        endif ()
        list(APPEND _FL_PLUGIN_QML_FILES "${_FL_PLUGIN_QML_ENTRY}")
        list(REMOVE_DUPLICATES _FL_PLUGIN_QML_FILES)
        foreach (_framelift_qml_file IN LISTS _FL_PLUGIN_QML_FILES)
            get_filename_component(_framelift_qml_alias "${_framelift_qml_file}" NAME)
            set_source_files_properties(
                    "${_framelift_qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${_framelift_qml_alias}")
        endforeach ()
        string(REPLACE "." "/" _framelift_qml_uri_path "${_FL_PLUGIN_QML_URI}")
        get_filename_component(_framelift_qml_entry_name "${_FL_PLUGIN_QML_ENTRY}" NAME)
        target_compile_definitions(
                ${NAME} PRIVATE
                FRAMELIFT_QML_ENTRY_URL="qrc:/qt/qml/${_framelift_qml_uri_path}/${_framelift_qml_entry_name}")
        qt_add_qml_module(
                ${NAME}
                URI "${_FL_PLUGIN_QML_URI}"
                VERSION 1.0
                RESOURCE_PREFIX "/qt/qml"
                IMPORT_PATH "${CMAKE_BINARY_DIR}"
                OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${_framelift_qml_uri_path}"
                NO_PLUGIN
                QML_FILES ${_FL_PLUGIN_QML_FILES})
    else ()
        target_compile_definitions(${NAME} PRIVATE FRAMELIFT_QML_ENTRY_URL=nullptr)
    endif ()
    # CMAKE_CURRENT_BINARY_DIR holds the generated metadata JSON; on the target's include
    # path so moc resolves Q_PLUGIN_METADATA(... FILE "<NAME>PluginMetadata.json").
    # CMAKE_CURRENT_SOURCE_DIR (the calling plugin's dir) lets the plugin include its own
    # headers without each plugin repeating the line.
    target_include_directories(${NAME} PRIVATE
            "${CMAKE_CURRENT_BINARY_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(${NAME} PRIVATE FrameLiftSdk)
    if (MINGW)
        target_link_options(${NAME} PRIVATE
                -static-libgcc -static-libstdc++ -static)
    endif ()
    if (TARGET FrameLift)
        set(_framelift_plugin_out "$<TARGET_FILE_DIR:FrameLift>/plugins")
    else ()
        set(_framelift_plugin_out "${CMAKE_BINARY_DIR}/plugins")
    endif ()
    # RUNTIME covers the Windows .dll; LIBRARY covers the Linux/macOS .so/.dylib
    # (a SHARED lib is a LIBRARY artifact off-Windows). Set both so plugins always
    # land in plugins/ regardless of platform.
    set_target_properties(${NAME} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${_framelift_plugin_out}"
            LIBRARY_OUTPUT_DIRECTORY "${_framelift_plugin_out}")
    # Ensure the plugins/ output directory exists before linking.
    add_custom_command(TARGET ${NAME} PRE_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_framelift_plugin_out}")
endfunction()
