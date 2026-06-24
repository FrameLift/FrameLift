#pragma once

#include <cstdint>

// A single snapshot of process-level OS performance counters.
// All fields describe THIS process (FrameLift), not the whole machine.
struct SysSample
{
    double cpuPercent = 0.0;  // CPU usage, normalized across all cores (0-100)
    uint64_t memBytes = 0;    // resident memory / working set, in bytes
    double gpuPercent = 0.0;  // best-effort GPU utilization (0-100)
    bool gpuValid = false;    // false → GPU usage unavailable, display "N/A"
};

// Cross-platform sampler for process CPU / memory / GPU usage.
//
// Self-contained in the Benchmark plugin (SDK helper sources are compiled
// per-plugin, so nothing here is shared). CPU and GPU are rate counters that
// need two readings: the sampler caches the previous reading and computes the
// delta on each Sample(), so callers should poll at a steady cadence (~1/s).
class SysSampler
{
public:
    SysSampler();
    ~SysSampler();

    SysSampler(const SysSampler&) = delete;
    SysSampler& operator=(const SysSampler&) = delete;

    // Reads the current counters and returns a normalized snapshot.
    // The first call establishes a baseline for the rate counters (CPU/GPU),
    // so its cpuPercent/gpuPercent may read 0 until the next call.
    SysSample Sample();

    // Opaque platform-specific state, defined only in SysStats.cpp. Public so
    // file-local helpers in the .cpp can name SysSampler::Impl; still opaque to
    // callers since it has no definition in this header.
    struct Impl;

private:
    Impl* impl_ = nullptr; // PDH handles, previous CPU times, core count, ...
};
