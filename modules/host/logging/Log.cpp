#include <framelift/Log.h>

#include <QtCore/QDebug>

#include "LogBuffer.h"

namespace
{
// Host sink: receives formatted lines from the SDK (host + every plugin) and
// routes them to Qt logging. Installed into the host TU by Init() and into each
// plugin DLL by the loader via IPackage::SetLogSink.
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
    }
}
} // namespace

Log::SinkFn HostLogSink()
{
    return &QtLogSink;
}

void Log::Init()
{
    qSetMessagePattern(
        "[%{time hh:mm:ss.zzz}] [%{if-debug}DEBUG%{endif}%{if-info}INFO %{endif}%{if-warning}WARN %{endif}"
        "%{if-critical}ERROR%{endif}%{if-fatal}FATAL%{endif}] %{message}"
    );

    // Route the host's own Log::* calls into Qt logging.
    SetSink(&QtLogSink);
}
