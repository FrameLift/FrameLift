# Resolves the host's external platform libraries — SDL3 (window/audio/input),
# FFmpeg (decode/filter), and libass (subtitles) — and defines the link targets
# the host uses: SDL3::SDL3, `ffmpeg`, and `libass`.
#
#  • Linux: system dev packages (libsdl3-dev, libavcodec-dev, libass-dev, ...) via
#    find_package / pkg-config.
#  • Windows: vcpkg manifest mode (see vcpkg.json). The vcpkg toolchain installs the
#    libraries on configure and seeds the include/lib search paths. Runtime DLLs and
#    their transitive deps are deployed next to framelift.exe automatically by vcpkg's
#    app-local deployment (VCPKG_APPLOCAL_DEPS) — no manual DLL copy needed. Requires
#    configuring with the vcpkg toolchain (see CMakePresets.json).

# ── SDL3 ──────────────────────────────────────────────────────────────────────
# vcpkg is used on Windows only. On Linux SDL3 comes from the native package
# manager (libsdl3-dev) — no vcpkg involved. Both ship an SDL3 CMake config, so the
# same find_package resolves it either way (vcpkg only kicks in when its toolchain
# is active, i.e. the Windows build). Resolved first because the imgui static lib
# (FrameLiftDeps.cmake) links SDL3::SDL3.
find_package(SDL3 REQUIRED CONFIG)

# ── FFmpeg ────────────────────────────────────────────────────────────────────
if (WIN32)
    # FFmpeg from vcpkg. vcpkg's FindFFMPEG.cmake isn't placed on CMAKE_MODULE_PATH,
    # so resolve the headers + per-component import libs directly — the vcpkg
    # toolchain has seeded the find_ search paths with the installed prefix.
    find_path(FFMPEG_INCLUDE_DIR libavcodec/avcodec.h REQUIRED)
    add_library(ffmpeg INTERFACE)
    target_include_directories(ffmpeg INTERFACE ${FFMPEG_INCLUDE_DIR})
    foreach (_lib avformat avcodec avutil swscale swresample avfilter)
        find_library(FFMPEG_${_lib}_LIBRARY ${_lib} REQUIRED)
        target_link_libraries(ffmpeg INTERFACE ${FFMPEG_${_lib}_LIBRARY})
    endforeach ()
else ()
    # Hardware decode (issue #25) relies on the system ffmpeg shipping the hwaccels
    # used at runtime — nvdec/cuvid for NVIDIA and vaapi for Intel/AMD. Distro ffmpeg
    # packages normally include both; the FFmpegHwDecode helper degrades to software
    # if a backend is unavailable, so this is a runtime expectation, not a build dep.
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
            libavformat libavcodec libavutil libswscale libswresample libavfilter)
    add_library(ffmpeg ALIAS PkgConfig::FFMPEG)
endif ()

# ── libass (subtitles) ────────────────────────────────────────────────────────
if (WIN32)
    # libass ships no CMake config / vcpkg usage file, so resolve the header + import
    # lib directly. The vcpkg toolchain has added its prefix to the find_ search paths.
    find_path(ASS_INCLUDE_DIR ass/ass.h REQUIRED)
    find_library(ASS_LIBRARY NAMES ass libass REQUIRED)
    add_library(libass INTERFACE)
    target_include_directories(libass INTERFACE ${ASS_INCLUDE_DIR})
    target_link_libraries(libass INTERFACE ${ASS_LIBRARY})
else ()
    pkg_check_modules(LIBASS REQUIRED IMPORTED_TARGET libass)
    add_library(libass ALIAS PkgConfig::LIBASS)
endif ()
