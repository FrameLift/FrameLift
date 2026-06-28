#include <framelift/Log.h>

#include <QtCore/QDebug>
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

    // Route the host's own Log::* calls into Qt logging.
    SetSink(&QtLogSink);
}
