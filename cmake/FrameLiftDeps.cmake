# Third-party dependencies for the HOST build only.
#
# These are NOT part of the public plugin SDK — after the COM-like ABI redesign,
# nothing crosses the host↔plugin boundary as a third-party type, so plugins do
# not need stb/Vulkan dependencies. JSON is a host capability too: Qt's QJson backs
# the host IJson service (modules/host/services/JsonServiceImpl), and plugins reach
# it via ctx.GetService<IJson>() — no plugin links a JSON library.
# FFmpeg and libass are set up by FrameLiftPlatformLibs.cmake as the external
# playback stack; Qt6 (window/QML/raw PCM audio, and QJson for the JSON backend)
# is resolved in the root CMakeLists.txt.

include(FetchContent)

# ── Vulkan stack (second graphics backend — OpenGL→Vulkan migration, #17) ───────
# Resolved here so the SAME setup works identically on the Windows MinGW cross-build
# and the native-Linux build (vcpkg only runs on Windows here). FrameLift links the
# official Vulkan loader when the Vulkan backend is enabled, while VMA compiles
# in-tree as a header-only allocator.
#
# Keep Vulkan-Headers and VMA tags coherent with the Vulkan API level FrameLift
# prefers; bump together if a tag ever 404s.

if (FRAMELIFT_MODULE_GRAPHICS_VULKAN)
    # Vulkan-Headers — the API headers + the Vulkan::Headers interface target that VMA
    # consumes. Qt may have already provided this target through FindVulkan; fetch the
    # headers only when no system/SDK target exists.
    if (NOT TARGET Vulkan::Headers)
        FetchContent_Declare(
                vulkan_headers
                GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
                GIT_TAG v1.4.354
                GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(vulkan_headers)
    endif ()
    if (NOT Vulkan_INCLUDE_DIR OR Vulkan_INCLUDE_DIR MATCHES "-NOTFOUND$")
        get_target_property(_framelift_vulkan_header_dirs Vulkan::Headers INTERFACE_INCLUDE_DIRECTORIES)
        list(GET _framelift_vulkan_header_dirs 0 _framelift_vulkan_include_dir)
        set(Vulkan_INCLUDE_DIR "${_framelift_vulkan_include_dir}" CACHE PATH "Vulkan headers include directory" FORCE)
        set(Vulkan_INCLUDE_DIRS "${Vulkan_INCLUDE_DIR}")
        unset(_framelift_vulkan_include_dir)
        unset(_framelift_vulkan_header_dirs)
    endif ()

    find_package(Vulkan QUIET)
    if (NOT TARGET Vulkan::Vulkan)
        find_library(
                FRAMELIFT_VULKAN_LOADER_LIBRARY
                NAMES vulkan vulkan-1 vulkan.so.1 libvulkan.so.1
        )
        if (NOT FRAMELIFT_VULKAN_LOADER_LIBRARY)
            message(FATAL_ERROR
                    "The Vulkan backend links the official Vulkan loader, but CMake could not find "
                    "libvulkan/vulkan-1. Install the Vulkan loader development package or disable "
                    "the backend with -DFRAMELIFT_MODULE_GRAPHICS_VULKAN=OFF.")
        endif ()

        add_library(Vulkan::Vulkan UNKNOWN IMPORTED)
        set_target_properties(Vulkan::Vulkan PROPERTIES
                IMPORTED_LOCATION "${FRAMELIFT_VULKAN_LOADER_LIBRARY}"
                INTERFACE_LINK_LIBRARIES Vulkan::Headers)
    endif ()

    # VMA — header-only GPU allocator. A single host TU defines VMA_IMPLEMENTATION.
    FetchContent_Declare(
            vma
            GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
            GIT_TAG v3.4.0
            GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(vma)
endif ()

# ── stb (header-only) ─────────────────────────────────────────────────────────
FetchContent_Declare(
        stb
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG master
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(stb)

# JSON: the IJson service (JsonServiceImpl) is backed by Qt6::Core's QJson — no
# third-party JSON library is fetched or linked.
