# Third-party dependencies for the HOST build only.
#
# These are NOT part of the public plugin SDK — after the COM-like ABI redesign,
# nothing crosses the host↔plugin boundary as a third-party type, so plugins do
# not need imgui/spdlog/stb, and only plugins that opt into JSON link nlohmann.
# SDL3, FFmpeg, and libass are set up by FrameLiftPlatformLibs.cmake before this module,
# because the imgui static library links SDL3::SDL3.

include(FetchContent)

# ── spdlog ────────────────────────────────────────────────────────────────────
FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.17.0
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(spdlog)

# ── Vulkan stack (second graphics backend — OpenGL→Vulkan migration, #17) ───────
# Resolved via FetchContent rather than vcpkg so the SAME setup works identically on
# the Windows MinGW cross-build and the native-Linux build (vcpkg only runs on
# Windows here). volk, VMA and vk-bootstrap are single-source / header-only and
# compile in-tree, exactly like imgui/spdlog above. Crucially, volk loads the Vulkan
# loader dynamically at runtime (volkInitialize), so there is NO link-time dependency
# on libvulkan — sidestepping the brittle MinGW loader-import path. The runtime loader
# (vulkan-1.dll / libvulkan.so.1) only needs to be present at run time, which it is
# wherever a GPU driver is installed.
#
# Tags are a coherent ~1.3.x set; bump together if a tag ever 404s.

# Vulkan-Headers — the API headers + the Vulkan::Headers interface target that volk,
# VMA and vk-bootstrap consume. Declared first so the others detect and reuse it.
if (FRAMELIFT_MODULE_GRAPHICS_VULKAN)
FetchContent_Declare(
        vulkan_headers
        GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
        GIT_TAG v1.4.354
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(vulkan_headers)

# volk — Vulkan meta-loader. Builds the `volk` static lib (the global entry-point
# pointers, linked once into the exe) and the `volk_headers` interface (include-only,
# linked into imgui so imgui_impl_vulkan can call through the same pointers).
FetchContent_Declare(
        volk
        GIT_REPOSITORY https://github.com/zeux/volk.git
        GIT_TAG 1.4.350
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(volk)

# VMA — header-only GPU allocator. A single host TU defines VMA_IMPLEMENTATION.
FetchContent_Declare(
        vma
        GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
        GIT_TAG v3.4.0
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(vma)

# vk-bootstrap — instance/physical-device/device/swapchain selection boilerplate.
FetchContent_Declare(
        vk_bootstrap
        GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap.git
        GIT_TAG v1.4.353
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(vk_bootstrap)
endif ()

# ── Dear ImGui ────────────────────────────────────────────────────────────────
# The docking-branch tag is a superset of the like-versioned release: it adds
# multi-viewport (ImGuiConfigFlags_ViewportsEnable + UpdatePlatformWindows/
# RenderPlatformWindowsDefault), which the host uses to pop panels out into their
# own OS windows.
FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.92.8-docking
        GIT_SHALLOW TRUE
)
# imgui ships no CMakeLists.txt; FetchContent_MakeAvailable silently skips
# add_subdirectory() in that case (CMake 3.28+), leaving imgui_SOURCE_DIR set.
FetchContent_MakeAvailable(imgui)

set(_FRAMELIFT_IMGUI_SOURCES
        "${imgui_SOURCE_DIR}/imgui.cpp"
        "${imgui_SOURCE_DIR}/imgui_draw.cpp"
        "${imgui_SOURCE_DIR}/imgui_tables.cpp"
        "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp"
)
if (FRAMELIFT_MODULE_GRAPHICS_VULKAN)
    list(APPEND _FRAMELIFT_IMGUI_SOURCES
            "${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp")
endif ()
add_library(imgui STATIC ${_FRAMELIFT_IMGUI_SOURCES})
target_include_directories(imgui PUBLIC
        "${imgui_SOURCE_DIR}"
        "${imgui_SOURCE_DIR}/backends"
)
target_link_libraries(imgui PUBLIC SDL3::SDL3)

# Route imgui_impl_vulkan through volk (no Vulkan prototypes linked): with
# IMGUI_IMPL_VULKAN_USE_VOLK the backend includes volk.h and calls the dynamically
# loaded entry points. PUBLIC so the host's Vulkan backend TU sees the same flag when
# it includes imgui_impl_vulkan.h. Only volk_headers here (include-only) — the single
# compiled `volk` definition is linked into the FrameLift exe.
if (FRAMELIFT_MODULE_GRAPHICS_VULKAN)
    target_compile_definitions(imgui PUBLIC IMGUI_IMPL_VULKAN_USE_VOLK)
    target_link_libraries(imgui PUBLIC volk_headers)
endif ()

# ── stb (header-only) ─────────────────────────────────────────────────────────
FetchContent_Declare(
        stb
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG master
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(stb)

# ── nlohmann/json (header-only) ───────────────────────────────────────────────
FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
        GIT_SHALLOW TRUE
)
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)
