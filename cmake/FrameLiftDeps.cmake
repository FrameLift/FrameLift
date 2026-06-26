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
# Resolved via FetchContent rather than vcpkg so the SAME setup works identically on
# the Windows MinGW cross-build and the native-Linux build (vcpkg only runs on
# Windows here). volk and VMA are single-source / header-only and
# compile in-tree. Crucially, volk loads the Vulkan
# loader dynamically at runtime (volkInitialize), so there is NO link-time dependency
# on libvulkan — sidestepping the brittle MinGW loader-import path. The runtime loader
# (vulkan-1.dll / libvulkan.so.1) only needs to be present at run time, which it is
# wherever a GPU driver is installed.
#
# Tags are a coherent ~1.3.x set; bump together if a tag ever 404s.

# Vulkan-Headers — the API headers + the Vulkan::Headers interface target that volk
# and VMA consume. Declared first so the others detect and reuse it.
if (FRAMELIFT_MODULE_GRAPHICS_VULKAN)
if (NOT TARGET Vulkan::Headers)
    FetchContent_Declare(
            vulkan_headers
            GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
            GIT_TAG v1.4.354
            GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(vulkan_headers)
endif ()

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
