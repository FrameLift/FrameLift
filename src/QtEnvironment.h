#pragma once

#include <QtCore/QByteArray>
#include <QtCore/qtenvironmentvariables.h>

// Apply process-wide Qt policy before QApplication initializes any platform,
// scene-graph, or multimedia services.
inline void ConfigureQtEnvironment()
{
    // Keep Qt Quick and FrameLift's raw-GL video renderer on the GUI thread.
    (void)qputenv("QSG_RENDER_LOOP", QByteArray("basic"));

    // FrameLift uses Qt Multimedia only for raw QAudioSink output. Its FFmpeg
    // plugin still probes decoding and encoding devices while initializing the
    // audio-device service, so disable both otherwise-unused hardware lists.
    // FrameLift's direct libavcodec hardware decoding ignores these Qt settings.
    (void)qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", QByteArray(","));
    (void)qputenv("QT_FFMPEG_ENCODING_HW_DEVICE_TYPES", QByteArray(","));
}
