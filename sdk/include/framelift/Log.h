#pragma once
#include <format>
#include <string>
#include <utility>

// Logging abstraction with NO third-party dependency. Callers use Log::* — the
// message is formatted in-plugin via std::format, then handed to the host across
// a POD C-ABI sink (void(*)(int, const char*)). The host routes it to its real
// logger (spdlog). Each translation unit holds its own sink pointer; the host
// installs it per-plugin via the framelift_set_log_sink export (see ModuleEntry.h).

namespace Log
{
enum class Level : int
{
    Debug,
    Info,
    Warn,
    Error
};

// POD sink: level is a Log::Level value, msg is a NUL-terminated, call-scoped string.
using SinkFn = void (*)(int level, const char* msg);

// Install the sink that receives formatted log lines. Called by the host on its
// own translation units (via Init) and on each plugin (via framelift_set_log_sink).
void SetSink(SinkFn fn);

// Format target — defined in sdk/src/Log.cpp; forwards to the installed sink.
void Emit(Level level, const std::string& msg);

// Host-only: configure the backing logger. Defined in modules/host/logging/Log.cpp.
void Init();

template <typename... Args>
void Debug(std::format_string<Args...> fmt, Args&&... args)
{
    Emit(Level::Debug, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void Info(std::format_string<Args...> fmt, Args&&... args)
{
    Emit(Level::Info, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void Warn(std::format_string<Args...> fmt, Args&&... args)
{
    Emit(Level::Warn, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void Error(std::format_string<Args...> fmt, Args&&... args)
{
    Emit(Level::Error, std::format(fmt, std::forward<Args>(args)...));
}
} // namespace Log
