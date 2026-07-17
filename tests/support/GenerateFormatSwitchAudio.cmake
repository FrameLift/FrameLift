# Generates FORMAT_SWITCH_AUDIO_FILE: an mp3 whose decoded format changes
# mid-stream (stereo 44.1 kHz, then mono 22.05 kHz) by concatenating two
# independently encoded segments — a legal mp3 byte stream, and the decoder-level
# format change that must rebuild the playback resampler instead of crashing
# (a mono frame pushed through a stereo-configured SwrContext dereferences a
# plane that doesn't exist). Included by RunLaunchAndCheck.cmake via SETUP_SCRIPT.

if (NOT DEFINED FORMAT_SWITCH_AUDIO_FILE)
    message(FATAL_ERROR "FORMAT_SWITCH_AUDIO_FILE is required")
endif ()

if (EXISTS "${FORMAT_SWITCH_AUDIO_FILE}")
    return()
endif ()

find_program(FFMPEG_EXECUTABLE ffmpeg REQUIRED)
get_filename_component(_dir "${FORMAT_SWITCH_AUDIO_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${_dir}")

set(_stereo "${FORMAT_SWITCH_AUDIO_FILE}.stereo.part")
set(_mono "${FORMAT_SWITCH_AUDIO_FILE}.mono.part")
foreach (_seg IN ITEMS
        "${_stereo};2;44100;440"
        "${_mono};1;22050;330")
    list(GET _seg 0 _out)
    list(GET _seg 1 _channels)
    list(GET _seg 2 _rate)
    list(GET _seg 3 _freq)
    execute_process(
            COMMAND "${FFMPEG_EXECUTABLE}" -y -loglevel error
            -f lavfi -i "sine=frequency=${_freq}:duration=2"
            -ac ${_channels} -ar ${_rate} -c:a libmp3lame -f mp3 "${_out}"
            RESULT_VARIABLE _rc)
    if (NOT _rc EQUAL 0)
        message(FATAL_ERROR "ffmpeg failed generating ${_out}")
    endif ()
endforeach ()

execute_process(
        COMMAND "${CMAKE_COMMAND}" -E cat "${_stereo}" "${_mono}"
        OUTPUT_FILE "${FORMAT_SWITCH_AUDIO_FILE}"
        RESULT_VARIABLE _rc)
file(REMOVE "${_stereo}" "${_mono}")
if (NOT _rc EQUAL 0)
    message(FATAL_ERROR "concatenating mp3 segments failed")
endif ()
