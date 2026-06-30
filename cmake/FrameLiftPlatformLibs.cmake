# Resolves the host's external platform libraries — FFmpeg (decode/filter) and
# libass (subtitles) — and defines the link targets the host uses: `ffmpeg` and
# `libass`. This is FrameLift's compressed-media pipeline; Qt Multimedia is
# resolved separately only for raw PCM output through QAudioSink.
#
#  • Linux: system dev packages (libavcodec-dev, libass-dev, ...) via
#    find_package / pkg-config.
#  • Windows: vcpkg manifest mode (see vcpkg.json). The vcpkg toolchain installs the
#    libraries on configure and seeds the include/lib search paths. Runtime DLLs and
#    their transitive deps are deployed next to framelift.exe automatically by vcpkg's
#    app-local deployment (VCPKG_APPLOCAL_DEPS) — no manual DLL copy needed. Requires
#    configuring with the vcpkg toolchain (see CMakePresets.json).

# ── FFmpeg ────────────────────────────────────────────────────────────────────
# The FFmpeg libraries FrameLift links, named without the lib prefix. pkg-config wants
# the lib-prefixed module names (libavcodec, …); the vcpkg import-lib lookup wants the
# bare names (avcodec, …).
set(_framelift_ffmpeg_components avformat avcodec avutil swscale swresample avfilter)
if (WIN32)
    # FFmpeg from vcpkg. vcpkg's FindFFMPEG.cmake isn't placed on CMAKE_MODULE_PATH,
    # so resolve the headers + per-component import libs directly — the vcpkg
    # toolchain has seeded the find_ search paths with the installed prefix.
    find_path(FFMPEG_INCLUDE_DIR libavcodec/avcodec.h REQUIRED)
    add_library(ffmpeg INTERFACE)
    target_include_directories(ffmpeg INTERFACE ${FFMPEG_INCLUDE_DIR})
    foreach (_lib IN LISTS _framelift_ffmpeg_components)
        find_library(FFMPEG_${_lib}_LIBRARY ${_lib} REQUIRED)
        target_link_libraries(ffmpeg INTERFACE ${FFMPEG_${_lib}_LIBRARY})
    endforeach ()
else ()
    # Hardware decode (issue #25) relies on the system ffmpeg shipping the hwaccels
    # used at runtime — nvdec/cuvid for NVIDIA and vaapi for Intel/AMD. Distro ffmpeg
    # packages normally include both; the FFmpegHwDecode helper degrades to software
    # if a backend is unavailable, so this is a runtime expectation, not a build dep.
    find_package(PkgConfig REQUIRED)
    set(_framelift_ffmpeg_pc_modules)
    foreach (_lib IN LISTS _framelift_ffmpeg_components)
        list(APPEND _framelift_ffmpeg_pc_modules "lib${_lib}")
    endforeach ()
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET ${_framelift_ffmpeg_pc_modules})
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
