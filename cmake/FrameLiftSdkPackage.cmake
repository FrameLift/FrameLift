# Stage + package the standalone, dependency-free plugin SDK.
#
# framelift_configure_sdk_package() stages the `sdk` install component and configures a
# CPack ZIP (FrameLift-sdk-<ver>.zip). Inert during a normal build; materialised via
# `cmake --install ... --component sdk` + `cpack` (see .github/workflows/ci.yml). The
# package version tracks the ABI version so find_package(FrameLiftSdk) gates on
# ExactVersion. framelift_read_abi_version() comes from FrameLiftPluginMetadata.cmake
# (included via the SDK module before this runs).

function(framelift_configure_sdk_package)
    # The version is parsed from the SDK header — single source of truth.
    set(_abi_header "${CMAKE_SOURCE_DIR}/sdk/include/framelift/ModuleABI.h")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_abi_header}")
    framelift_read_abi_version(_abi_version "${_abi_header}")

    include(CMakePackageConfigHelpers)
    configure_package_config_file(
            "${CMAKE_SOURCE_DIR}/cmake/FrameLiftSdkConfig.cmake.in"
            "${CMAKE_CURRENT_BINARY_DIR}/sdk-pkg/FrameLiftSdkConfig.cmake"
            INSTALL_DESTINATION "cmake")
    write_basic_package_version_file(
            "${CMAKE_CURRENT_BINARY_DIR}/sdk-pkg/FrameLiftSdkConfigVersion.cmake"
            VERSION "${_abi_version}"
            COMPATIBILITY ExactVersion)

    install(DIRECTORY "${CMAKE_SOURCE_DIR}/sdk/include/"
            DESTINATION include COMPONENT sdk)
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/sdk/src/"
            DESTINATION src COMPONENT sdk)
    install(FILES
            "${CMAKE_SOURCE_DIR}/cmake/FrameLiftSdk.cmake"
            "${CMAKE_SOURCE_DIR}/cmake/FrameLiftPluginMetadata.cmake"
            "${CMAKE_CURRENT_BINARY_DIR}/sdk-pkg/FrameLiftSdkConfig.cmake"
            "${CMAKE_CURRENT_BINARY_DIR}/sdk-pkg/FrameLiftSdkConfigVersion.cmake"
            DESTINATION cmake COMPONENT sdk)
    install(FILES "${CMAKE_SOURCE_DIR}/sdk/package-template/CMakeLists.txt"
            DESTINATION . COMPONENT sdk)
    install(FILES "${CMAKE_SOURCE_DIR}/sdk/README.md" "${CMAKE_SOURCE_DIR}/LICENSE"
            DESTINATION . COMPONENT sdk)

    # ── CPack: one ZIP for the sdk component → FrameLift-sdk-<ver>.zip ──────────────
    set(CPACK_GENERATOR "ZIP")
    set(CPACK_PACKAGE_NAME "FrameLift-sdk")
    set(CPACK_PACKAGE_VERSION "${FRAMELIFT_VERSION}")
    set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
    set(CPACK_COMPONENTS_ALL sdk)
    set(CPACK_ARCHIVE_SDK_FILE_NAME "FrameLift-sdk-${FRAMELIFT_VERSION}")
    include(CPack)
endfunction()
