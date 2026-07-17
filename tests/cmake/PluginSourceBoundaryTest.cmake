if (NOT DEFINED FRAMELIFT_ROOT)
    message(FATAL_ERROR "FRAMELIFT_ROOT is required")
endif ()

file(READ "${FRAMELIFT_ROOT}/plugins/settings-menu/CMakeLists.txt" settings_menu_cmake)
if (settings_menu_cmake MATCHES "modules/host|FRAMELIFT_HOST_MODULE_INCLUDE_DIRS|framelift_apply_builtin_module_definitions")
    message(FATAL_ERROR "SettingsMenu crossed the production host source boundary")
endif ()

file(READ "${FRAMELIFT_ROOT}/cmake/FrameLiftSdk.cmake" sdk_cmake)
if (NOT sdk_cmake MATCHES "may not compile host source")
    message(FATAL_ERROR "add_framelift_plugin no longer enforces the host source boundary")
endif ()
