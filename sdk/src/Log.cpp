#include <framelift/Log.h>

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

// Per-translation-unit sink pointer. Each plugin DLL (and the host) compiles its
// own copy of this TU; the host installs the sink into each via framelift_set_log_sink.

namespace
{
Log::SinkFn g_sink = nullptr;

// Name-keyed perf timers. Per-module (this TU is compiled into each plugin and the
// host); mutex-guarded because a timer may be started and ended on different threads.
std::mutex g_perfMutex;
std::unordered_map<std::string, std::chrono::steady_clock::time_point> g_perfTimers;

// Runtime gating, read once per translation unit from the environment. Host and every
// plugin live in one process, so they all observe the same FL_LOG_LEVEL / FL_LOG_PERF
// and gate consistently — no need to push a level across the plugin ABI.
struct LogConfig
{
    Log::Level minLevel = Log::Level::Info; // Debug hidden by default
    bool perf = false;                      // Perf logs/timers off by default
};

bool EnvTruthy(const char* v)
{
    if (!v)
    {
        return false;
    }
    return std::strcmp(v, "1") == 0 || std::strcmp(v, "true") == 0 || std::strcmp(v, "on") == 0 ||
           std::strcmp(v, "yes") == 0;
}

LogConfig ParseEnv()
{
    LogConfig cfg;

    if (const char* lvl = std::getenv("FL_LOG_LEVEL"))
    {
        std::string s(lvl);
        for (char& c : s)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (s == "debug")
        {
            cfg.minLevel = Log::Level::Debug;
        }
        else if (s == "info")
        {
            cfg.minLevel = Log::Level::Info;
        }
        else if (s == "warn" || s == "warning")
        {
            cfg.minLevel = Log::Level::Warn;
        }
        else if (s == "error")
        {
            cfg.minLevel = Log::Level::Error;
        }
        // Unknown/empty ⇒ leave the Info default.
    }

    cfg.perf = EnvTruthy(std::getenv("FL_LOG_PERF"));
    return cfg;
}

const LogConfig& Config()
{
    static const LogConfig cfg = ParseEnv(); // thread-safe init, computed once
    return cfg;
}
} // namespace

bool Log::IsEnabled(const Level level)
{
    const LogConfig& cfg = Config();
    if (level == Level::Perf)
    {
        return cfg.perf; // Perf is its own switch, not part of the severity ladder
    }
    return static_cast<int>(level) >= static_cast<int>(cfg.minLevel);
}

bool Log::PerfActive()
{
    return Config().perf;
}

void Log::SetSink(const SinkFn fn)
{
    g_sink = fn;
}

void Log::Emit(const Level level, const std::string& msg)
{
    if (g_sink)
    {
        g_sink(static_cast<int>(level), msg.c_str());
    }
}

void Log::Perf(const char* name, const double ms)
{
    Emit(Level::Perf, std::format("{} {:.1f} ms", name ? name : "", ms));
}

void Log::PerfStart(const char* name)
{
    if (!name || !PerfActive())
    {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard lock(g_perfMutex);
    g_perfTimers[name] = now; // latest-wins if already pending
}

double Log::PerfEnd(const char* name)
{
    if (!PerfActive())
    {
        return -1.0;
    }
    const auto now = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point start;
    {
        std::lock_guard lock(g_perfMutex);
        const auto it = name ? g_perfTimers.find(name) : g_perfTimers.end();
        if (it == g_perfTimers.end())
        {
            return -1.0; // no matching start — no-op, emit nothing
        }
        start = it->second;
        g_perfTimers.erase(it);
    }
    const double ms = std::chrono::duration<double, std::milli>(now - start).count();
    Perf(name, ms);
    return ms;
}
