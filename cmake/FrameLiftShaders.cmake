# Compiles the host's GLSL shaders to embedded SPIR-V byte-array headers at build time.
# Each shader becomes a header declaring `const uint32_t <varname>[]`, which the Vulkan
# renderer includes directly — so, like the GL renderer, there are no shader files to
# ship at runtime.
#
# The compiler is glslang. A *native* glslang is required: prefer one on PATH (the
# glslang-tools package / Vulkan SDK), and only fall back to building it in-tree via
# FetchContent. The PATH preference is essential when cross-compiling — the MinGW CI
# build is host-Linux→target-Windows, so a FetchContent glslang would be built for
# Windows and could not run on the Linux build host. The FetchContent fallback is for
# native builds (e.g. local MSVC) where no glslang is installed.

find_program(FRAMELIFT_GLSLANG NAMES glslang glslangValidator)

if (FRAMELIFT_GLSLANG)
    message(STATUS "FrameLift: using system glslang (${FRAMELIFT_GLSLANG})")
    set(_FRAMELIFT_GLSLANG_CMD "${FRAMELIFT_GLSLANG}")
    set(_FRAMELIFT_GLSLANG_DEP "")
else ()
    if (CMAKE_CROSSCOMPILING)
        message(FATAL_ERROR
                "No native glslang found and cross-compiling: install glslang-tools on the "
                "build host (a FetchContent glslang would target the wrong platform).")
    endif ()
    message(STATUS "FrameLift: no system glslang found; building it via FetchContent")
    include(FetchContent)
    # Build-time SPIR-V compiler only: disable the optimizer (avoids SPIRV-Tools), HLSL,
    # the remapper, tests and install — just the `glslang` CLI (glslang-standalone).
    set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
    set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
    set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
    set(ENABLE_GLSLANG_BINARIES ON CACHE BOOL "" FORCE)
    set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
    set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
            glslang
            GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
            GIT_TAG 15.0.0
            GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(glslang)
    set(_FRAMELIFT_GLSLANG_CMD "$<TARGET_FILE:glslang-standalone>")
    set(_FRAMELIFT_GLSLANG_DEP glslang-standalone)
endif ()

set(FRAMELIFT_SHADER_GEN_DIR "${CMAKE_BINARY_DIR}/generated/shaders")
file(MAKE_DIRECTORY "${FRAMELIFT_SHADER_GEN_DIR}")

# framelift_compile_shader(<src> <varname> <out-header-path-var>)
#   Compiles <src> (a .vert/.frag) to a SPIR-V C header declaring
#   `const uint32_t <varname>[]` and returns the generated header path.
function(framelift_compile_shader src varname out_var)
    get_filename_component(_name "${src}" NAME)
    set(_hdr "${FRAMELIFT_SHADER_GEN_DIR}/${_name}.spv.h")
    add_custom_command(
            OUTPUT "${_hdr}"
            # -V: Vulkan SPIR-V; --vn: emit a C array named <varname>.
            COMMAND "${_FRAMELIFT_GLSLANG_CMD}" -V --vn ${varname} -o "${_hdr}" "${src}"
            DEPENDS "${src}" ${_FRAMELIFT_GLSLANG_DEP}
            COMMENT "Compiling shader ${_name} -> SPIR-V header"
            VERBATIM
    )
    set(${out_var} "${_hdr}" PARENT_SCOPE)
endfunction()
