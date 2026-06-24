#include <framelift/Log.h>
#include <memory>
#include <utility>

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "LogBuffer.h"

namespace
{
// Host sink: receives formatted lines from the SDK (host + every plugin) and
// routes them to spdlog. Installed into the host TU by Init() and into each
// plugin DLL by the loader via framelift_set_log_sink.
void SpdlogSink(const int level, const char* msg)
{
    // Keep an in-memory copy for the Log Viewer plugin (read back via ILogBuffer).
    HostLogBuffer().Push(level, msg);

    switch (static_cast<Log::Level>(level))
    {
    case Log::Level::Debug:
        spdlog::debug("{}", msg);
        break;
    case Log::Level::Info:
        spdlog::info("{}", msg);
        break;
    case Log::Level::Warn:
        spdlog::warn("{}", msg);
        break;
    case Log::Level::Error:
        spdlog::error("{}", msg);
        break;
    }
}
} // namespace

Log::SinkFn HostLogSink()
{
    return &SpdlogSink;
}

void Log::Init()
{
    auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>("framelift", std::move(sink));
    logger->set_level(spdlog::level::debug);
    logger->set_pattern("[%H:%M:%S.%e] [%^%-5l%$] %v");
    spdlog::set_default_logger(std::move(logger));

    // Route the host's own Log::* calls into spdlog.
    SetSink(&SpdlogSink);
}
