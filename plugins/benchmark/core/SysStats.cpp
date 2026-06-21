#include "SysStats.h"

#include <algorithm>
#include <chrono>

// ── Windows ─────────────────────────────────────────────────────────────────
#if defined(_WIN32)

#include <windows.h>

#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>

#include <string>
#include <string_view>
#include <vector>

struct SysSampler::Impl
{
    // CPU: process kernel+user time vs. wall time, normalized across cores.
    bool cpuPrimed = false;
    uint64_t prevCpu100ns = 0; // kernel+user, in 100-ns units
    std::chrono::steady_clock::time_point prevWall{};
    DWORD numProcessors = 1;

    // GPU: PDH wildcard query summed over this process's 3D engine instances.
    PDH_HQUERY query = nullptr;
    PDH_HCOUNTER counter = nullptr;
    bool gpuAvailable = false;
    bool gpuInitTried = false; // PDH query opened lazily on first SampleGpu
    std::string pidNeedle;     // "pid_<id>" — filters instances belonging to us
};

static uint64_t FileTimeToU64(const FILETIME& ft)
{
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return u.QuadPart;
}

SysSampler::SysSampler() : impl_(new Impl)
{
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    impl_->numProcessors = std::max<DWORD>(1, si.dwNumberOfProcessors);

    impl_->pidNeedle = "pid_" + std::to_string(GetCurrentProcessId());
}

// Open the PDH GPU query on first use. Priming the wildcard GPU counter
// enumerates every GPU engine instance — hundreds of ms on the first call — so
// it is deferred out of the constructor (which runs at plugin load) to here,
// reached only once the benchmark overlay is actually sampling.
static void EnsureGpuQuery(SysSampler::Impl& s)
{
    if (s.gpuInitTried)
    {
        return;
    }
    s.gpuInitTried = true;

    if (PdhOpenQueryA(nullptr, 0, &s.query) == ERROR_SUCCESS)
    {
        const PDH_STATUS st =
            PdhAddCounterA(s.query, "\\GPU Engine(*engtype_3D)\\Utilization Percentage", 0, &s.counter);
        if (st == ERROR_SUCCESS)
        {
            // Prime the counter so the next Sample() has a delta to format.
            PdhCollectQueryData(s.query);
            s.gpuAvailable = true;
        }
        else
        {
            PdhCloseQuery(s.query);
            s.query = nullptr;
        }
    }
}

SysSampler::~SysSampler()
{
    if (impl_ && impl_->query)
    {
        PdhCloseQuery(impl_->query);
    }
    delete impl_;
}

static double SampleGpu(SysSampler::Impl& s)
{
    EnsureGpuQuery(s);
    if (!s.gpuAvailable || PdhCollectQueryData(s.query) != ERROR_SUCCESS)
    {
        return -1.0;
    }

    DWORD bufSize = 0;
    DWORD itemCount = 0;
    PDH_STATUS st = PdhGetFormattedCounterArrayA(s.counter, PDH_FMT_DOUBLE, &bufSize, &itemCount, nullptr);
    if (st != PDH_MORE_DATA || bufSize == 0)
    {
        return -1.0;
    }

    std::vector<uint8_t> buf(bufSize);
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_A*>(buf.data());
    if (PdhGetFormattedCounterArrayA(s.counter, PDH_FMT_DOUBLE, &bufSize, &itemCount, items) != ERROR_SUCCESS)
    {
        return -1.0;
    }

    double total = 0.0;
    for (DWORD i = 0; i < itemCount; ++i)
    {
        const auto& item = items[i];
        if (item.FmtValue.CStatus != PDH_CSTATUS_VALID_DATA || item.szName == nullptr)
        {
            continue;
        }
        // Instance names look like "pid_1234_luid_..._engtype_3D"; keep ours only.
        if (std::string_view(item.szName).find(s.pidNeedle) != std::string_view::npos)
        {
            total += item.FmtValue.doubleValue;
        }
    }
    return std::clamp(total, 0.0, 100.0);
}

SysSample SysSampler::Sample()
{
    SysSample out;
    Impl& s = *impl_;

    // ── Memory: working set ──────────────────────────────────────────────────
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
    {
        out.memBytes = pmc.WorkingSetSize;
    }

    // ── CPU: kernel+user delta over wall delta, normalized across cores ──────
    FILETIME ftCreate, ftExit, ftKernel, ftUser;
    if (GetProcessTimes(GetCurrentProcess(), &ftCreate, &ftExit, &ftKernel, &ftUser))
    {
        const uint64_t cpu100ns = FileTimeToU64(ftKernel) + FileTimeToU64(ftUser);
        const auto now = std::chrono::steady_clock::now();
        if (s.cpuPrimed)
        {
            const double wallSec = std::chrono::duration<double>(now - s.prevWall).count();
            const double cpuSec = static_cast<double>(cpu100ns - s.prevCpu100ns) / 1e7;
            if (wallSec > 0.0)
            {
                out.cpuPercent = std::clamp(cpuSec / wallSec / s.numProcessors * 100.0, 0.0, 100.0);
            }
        }
        s.prevCpu100ns = cpu100ns;
        s.prevWall = now;
        s.cpuPrimed = true;
    }

    // ── GPU: best-effort ─────────────────────────────────────────────────────
    const double gpu = SampleGpu(s);
    if (gpu >= 0.0)
    {
        out.gpuPercent = gpu;
        out.gpuValid = true;
    }

    return out;
}

// ── Linux (and other POSIX) ─────────────────────────────────────────────────
#else

#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

struct SysSampler::Impl
{
    bool cpuPrimed = false;
    uint64_t prevJiffies = 0; // utime + stime, in clock ticks
    std::chrono::steady_clock::time_point prevWall{};
    long ticksPerSec = 100;
    long numProcessors = 1;
};

SysSampler::SysSampler() : impl_(new Impl)
{
    impl_->ticksPerSec = std::max<long>(1, sysconf(_SC_CLK_TCK));
    impl_->numProcessors = std::max<long>(1, sysconf(_SC_NPROCESSORS_ONLN));
}

SysSampler::~SysSampler()
{
    delete impl_;
}

// Returns utime+stime in clock ticks from /proc/self/stat, or 0 on failure.
static uint64_t ReadProcessJiffies()
{
    FILE* f = std::fopen("/proc/self/stat", "r");
    if (!f)
    {
        return 0;
    }
    std::string contents;
    char chunk[512];
    size_t n;
    while ((n = std::fread(chunk, 1, sizeof(chunk), f)) > 0)
    {
        contents.append(chunk, n);
    }
    std::fclose(f);

    // comm (field 2) is wrapped in parens and may contain spaces/parens; the
    // numeric fields begin after the LAST ')'. utime/stime are fields 14/15,
    // i.e. tokens 11/12 (0-based) of the remainder.
    const auto rparen = contents.rfind(')');
    if (rparen == std::string::npos)
    {
        return 0;
    }

    uint64_t utime = 0, stime = 0;
    int idx = 0;
    const char* p = contents.c_str() + rparen + 1;
    while (*p)
    {
        while (*p == ' ')
        {
            ++p;
        }
        if (!*p)
        {
            break;
        }
        const char* tok = p;
        while (*p && *p != ' ')
        {
            ++p;
        }
        // Tokens after the last ')' are 0-based: idx 11 = utime, 12 = stime.
        if (idx == 11)
        {
            utime = std::strtoull(tok, nullptr, 10);
        }
        else if (idx == 12)
        {
            stime = std::strtoull(tok, nullptr, 10);
            break;
        }
        ++idx;
    }
    return utime + stime;
}

static uint64_t ReadResidentBytes()
{
    FILE* f = std::fopen("/proc/self/statm", "r");
    if (!f)
    {
        return 0;
    }
    unsigned long long total = 0, resident = 0;
    const int got = std::fscanf(f, "%llu %llu", &total, &resident);
    std::fclose(f);
    if (got < 2)
    {
        return 0;
    }
    return resident * static_cast<uint64_t>(sysconf(_SC_PAGESIZE));
}

// Best-effort GPU utilization via sysfs (AMD exposes gpu_busy_percent).
// Returns -1 when unavailable.
static double ReadGpuBusyPercent()
{
    FILE* f = std::fopen("/sys/class/drm/card0/device/gpu_busy_percent", "r");
    if (!f)
    {
        return -1.0;
    }
    int pct = -1;
    const int got = std::fscanf(f, "%d", &pct);
    std::fclose(f);
    if (got < 1 || pct < 0)
    {
        return -1.0;
    }
    return std::clamp(static_cast<double>(pct), 0.0, 100.0);
}

SysSample SysSampler::Sample()
{
    SysSample out;
    Impl& s = *impl_;

    out.memBytes = ReadResidentBytes();

    const uint64_t jiffies = ReadProcessJiffies();
    const auto now = std::chrono::steady_clock::now();
    if (s.cpuPrimed)
    {
        const double wallSec = std::chrono::duration<double>(now - s.prevWall).count();
        const double cpuSec = static_cast<double>(jiffies - s.prevJiffies) / s.ticksPerSec;
        if (wallSec > 0.0)
        {
            out.cpuPercent = std::clamp(cpuSec / wallSec / s.numProcessors * 100.0, 0.0, 100.0);
        }
    }
    s.prevJiffies = jiffies;
    s.prevWall = now;
    s.cpuPrimed = true;

    const double gpu = ReadGpuBusyPercent();
    if (gpu >= 0.0)
    {
        out.gpuPercent = gpu;
        out.gpuValid = true;
    }

    return out;
}

#endif
