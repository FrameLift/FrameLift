#include <framelift/Log.h>

#include <chrono>
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
    Info("[perf] {} {:.1f} ms", name ? name : "", ms);
}

void Log::PerfStart(const char* name)
{
    if (!name)
    {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard lock(g_perfMutex);
    g_perfTimers[name] = now; // latest-wins if already pending
}

double Log::PerfEnd(const char* name)
{
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
