#include <framelift/Log.h>

#include <QtCore/QDebug>
#include <QtCore/QString>
#include <QtCore/QTime>
#include <cstdio>

#include "LogBuffer.h"

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#define FL_ISATTY(fd) _isatty(fd)
#define FL_FILENO(f) _fileno(f)
#else
#include <unistd.h>
#define FL_ISATTY(fd) isatty(fd)
#define FL_FILENO(f) fileno(f)
#endif

namespace
{
// Whether stderr is a terminal that should get ANSI color. Computed once at Init;
// on Windows this also flips the console into virtual-terminal mode so the codes
// render instead of printing literally. False when redirected to a file/pipe, so
// captured logs (and the Log Viewer buffer, which stores the raw message) stay clean.
bool DetectColor()
{
    if (!FL_ISATTY(FL_FILENO(stderr)))
    {
        return false;
    }
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    DWORD mode = 0;
    if (h == INVALID_HANDLE_VALUE || !GetConsoleMode(h, &mode))
    {
        return false;
    }
    if (!(mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING))
    {
        SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
#endif
    return true;
}

bool g_color = false;

// Substrings of third-party log lines we deliberately drop from stderr. These still
// reach the Log Viewer buffer (HostLogBuffer::Push runs before this filter), so the
// suppression only quiets the console — we keep the data in case we want it later.
//   - "spaVisitChoice": Qt Multimedia's PipeWire audio backend emits this via
//     qWarning() on the *default* category (so QT_LOGGING_RULES can't target it) every
//     time QAudioSink/QMediaDevices probes a device advertising an IEC958 / S-PDIF
//     passthrough format — harmless pod-parse noise on playback start.
//   - "Using Qt multimedia with FFmpeg": Qt's FFmpeg media backend logs this banner on
//     init; we only use Qt Multimedia for audio output, so it's irrelevant noise.
//   - "Got leave event for surface 0x0": Qt's Wayland client (QWaylandTextInputv3) warns
//     when the compositor sends a text-input-v3 leave for a null surface at startup — a
//     known harmless upstream quirk. Matching on "0x0" keeps genuine mismatches visible.
//   - "fontselect: Using default font family": libass's routine font-fallback notice
//     (routed in via AssLogCallback in FFmpegSubtitles at warn level) — expected
//     substitution info, not a problem.
//   - "Failed to register with host portal": Qt's xdg-desktop-portal app-ID registration
//     fails whenever framelift.desktop isn't installed in the XDG data dirs (i.e. every
//     dev-tree run) — harmless; registration works on installed copies.
//   - The rest are benign libav* container quirks (routed in via FFmpegLogCallback):
//     mov UDTA atoms it can't parse, dangling QT chapter-track references, and the
//     "couldn't probe codec params / increase analyzeduration" pair on lazy-init files.
bool IsSuppressed(const QString& msg)
{
    return msg.contains(QLatin1String("spaVisitChoice")) ||
           msg.contains(QLatin1String("Using Qt multimedia with FFmpeg")) ||
           msg.contains(QLatin1String("Got leave event for surface 0x0")) ||
           msg.contains(QLatin1String("fontselect: Using default font family")) ||
           msg.contains(QLatin1String("Failed to register with host portal")) ||
           msg.contains(QLatin1String("UDTA parsing failed")) ||
           msg.contains(QLatin1String("Referenced QT chapter track not found")) ||
           msg.contains(QLatin1String("Could not find codec parameters for stream")) ||
           msg.contains(QLatin1String("Consider increasing the value for the"));
}

// Single output point for Qt logging. Applies the pattern set in Init(), drops suppressed
// third-party noise, and writes the rest to stderr. Host Log::* calls reach here through
// QtLogSink -> qWarning/etc.; Qt's own internal warnings arrive here directly.
void FilterMessageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg)
{
    if (IsSuppressed(msg))
    {
        return;
    }
    const QString formatted = qFormatLogMessage(type, ctx, msg);
    std::fprintf(stderr, "%s\n", qPrintable(formatted));
    std::fflush(stderr);
}

// Host sink: receives formatted lines from the SDK (host + every plugin) and
// routes them to Qt logging. Installed into the host TU by Init() and into each
// plugin DLL by the loader via IPlugin::SetLogSink.
void QtLogSink(const int level, const char* msg)
{
    // Keep an in-memory copy for the Log Viewer plugin (read back via ILogBuffer).
    HostLogBuffer().Push(level, msg);

    const char* line = msg ? msg : "";
    switch (static_cast<Log::Level>(level))
    {
    case Log::Level::Debug:
        qDebug().noquote() << line;
        break;
    case Log::Level::Info:
        qInfo().noquote() << line;
        break;
    case Log::Level::Warn:
        qWarning().noquote() << line;
        break;
    case Log::Level::Error:
        qCritical().noquote() << line;
        break;
    case Log::Level::Perf: {
        const QString now = QTime::currentTime().toString("hh:mm:ss.zzz");
        // Perf bypasses Qt logging, so colorize its tag (magenta) here directly.
        const char* tag = g_color ? "\033[35mPERF \033[0m" : "PERF ";
        std::fprintf(stderr, "[%s] [%s] %s\n", qPrintable(now), tag, line);
        std::fflush(stderr);
        break;
    }
    }
}
} // namespace

Log::SinkFn HostLogSink()
{
    return &QtLogSink;
}

void Log::Init()
{
    g_color = DetectColor();

    // Color each level tag via ANSI SGR codes; the message body is left at the
    // terminal's default color. The plain pattern is used when stderr isn't a TTY.
    if (g_color)
    {
        qSetMessagePattern(
            "[%{time hh:mm:ss.zzz}] ["
            "%{if-debug}\033[90mDEBUG\033[0m%{endif}"    // gray
            "%{if-info}\033[32mINFO \033[0m%{endif}"     // green
            "%{if-warning}\033[33mWARN \033[0m%{endif}"  // yellow
            "%{if-critical}\033[31mERROR\033[0m%{endif}" // red
            "%{if-fatal}\033[1;31mFATAL\033[0m%{endif}"  // bold red
            "] %{message}"
        );
    }
    else
    {
        qSetMessagePattern(
            "[%{time hh:mm:ss.zzz}] [%{if-debug}DEBUG%{endif}%{if-info}INFO %{endif}%{if-warning}WARN %{endif}"
            "%{if-critical}ERROR%{endif}%{if-fatal}FATAL%{endif}] %{message}"
        );
    }

    // Take over the final output stage so we can drop noisy third-party warnings
    // (see FilterMessageHandler) that the pattern alone can't filter.
    qInstallMessageHandler(&FilterMessageHandler);

    // Route the host's own Log::* calls into Qt logging.
    SetSink(&QtLogSink);

    // Report the active gating (configured from FL_LOG_LEVEL / FL_LOG_PERF) so it's
    // obvious from the console what's enabled. Probe IsEnabled rather than re-reading
    // the env here — the SDK owns that parse.
    const char* level = IsEnabled(Level::Debug) ? "debug"
                        : IsEnabled(Level::Info) ? "info"
                        : IsEnabled(Level::Warn) ? "warn"
                                                 : "error";
    Info("log level: {}  perf: {}", level, PerfActive() ? "on" : "off");
}
