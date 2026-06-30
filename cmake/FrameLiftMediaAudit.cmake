# Build-time guard: FrameLift plays compressed media through external FFmpeg, never
# Qt Multimedia's playback classes (Qt Multimedia is linked only for QAudioSink /
# QMediaDevices raw PCM output). framelift_assert_no_qt_media_playback() fails the
# configure if any host/plugin source or QML reintroduces a forbidden playback API.

function(framelift_assert_no_qt_media_playback)
    set(_forbidden
            "QMediaPlayer"
            "QVideoSink"
            "QMediaCaptureSession"
            "QMediaRecorder"
            "QCamera"
            "QImageCapture"
            "QAudioOutput")
    file(GLOB_RECURSE _audit_files CONFIGURE_DEPENDS
            "${CMAKE_SOURCE_DIR}/src/*.[ch]pp"
            "${CMAKE_SOURCE_DIR}/src/*.h"
            "${CMAKE_SOURCE_DIR}/modules/*.[ch]pp"
            "${CMAKE_SOURCE_DIR}/modules/*.h"
            "${CMAKE_SOURCE_DIR}/plugins/*.[ch]pp"
            "${CMAKE_SOURCE_DIR}/plugins/*.h"
            "${CMAKE_SOURCE_DIR}/qml/*.qml"
            "${CMAKE_SOURCE_DIR}/plugins/*.qml")
    foreach (_file IN LISTS _audit_files)
        file(READ "${_file}" _text)
        foreach (_api IN LISTS _forbidden)
            if (_text MATCHES "${_api}")
                message(FATAL_ERROR
                        "FrameLift uses external FFmpeg for media playback; ${_file} "
                        "references forbidden Qt media playback API ${_api}. "
                        "Qt Multimedia is allowed only for QAudioSink/QMediaDevices audio output.")
            endif ()
        endforeach ()
        if (_file MATCHES "\\.qml$" AND _text MATCHES "import[ \t]+QtMultimedia")
            message(FATAL_ERROR
                    "FrameLift uses external FFmpeg for media playback; ${_file} "
                    "imports QtMultimedia QML. Qt Multimedia is allowed only for "
                    "QAudioSink/QMediaDevices audio output in C++.")
        endif ()
    endforeach ()
endfunction()
