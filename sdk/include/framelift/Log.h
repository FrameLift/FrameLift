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

// ── Performance timing ──────────────────────────────────────────────────────
// Lightweight, name-keyed timing. PerfStart stamps the current time under `name`;
// PerfEnd logs the elapsed milliseconds as a consistent "[perf] <name> <ms> ms"
// Info line and returns that duration. PerfEnd is a no-op returning <0 when `name`
// was never started, so an end site that fires repeatedly (e.g. once per frame)
// only emits the first time after a matching start. The registry is per-module
// (compiled into each plugin and the host); start/end must pair within one module.
// Use the FRAMELIFT_PERF_START / FRAMELIFT_PERF_END macros below.
void Perf(const char* name, double ms);
void PerfStart(const char* name);
double PerfEnd(const char* name);
} // namespace Log

#define FRAMELIFT_PERF_START(name) ::Log::PerfStart(name)
#define FRAMELIFT_PERF_END(name) ::Log::PerfEnd(name)
