#pragma once

#include <framelift/core.h>

#include "SysStats.h"

#include <QtCore/QObject>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class QTimer;
class BenchmarkSettings;

// Running min/avg/max aggregate over one sampled metric. Folded once per
// sample while a benchmark run is recording; reset between runs.
struct Stat
{
    double min = 0.0;
    double max = 0.0;
    double sum = 0.0;
    double sumSq = 0.0;
    uint64_t count = 0;

    void Reset()
    {
        *this = Stat{};
    }

    void Add(const double v)
    {
        min = count == 0 ? v : std::min(min, v);
        max = count == 0 ? v : std::max(max, v);
        sum += v;
        sumSq += v * v;
        ++count;
    }

    [[nodiscard]] double Avg() const
    {
        return count > 0 ? sum / static_cast<double>(count) : 0.0;
    }

    // Population standard deviation. 0 for fewer than two samples.
    [[nodiscard]] double Std() const
    {
        if (count < 2)
        {
            return 0.0;
        }
        const double n = static_cast<double>(count);
        const double variance = std::max(0.0, sumSq / n - (sum / n) * (sum / n));
        return std::sqrt(variance);
    }
};

// Performance benchmark window (F10 to toggle).
// Shows a live UI frame-rate/frame-time readout plus, once a run has recorded
// samples, the aggregated results that make a benchmark useful: FPS and frame
// time avg/min/max, frame-time standard deviation, 1% and 0.1% low FPS
// (percentiles of the slowest frames), and a stutter count (frames over 2× the
// average). Process performance (CPU %, memory, GPU %) is reported as a live
// value with its run avg/max, and a context block records the decoder, graphics
// backend, video resolution, and decode/cache health (dropped, mistimed, decode
// errors, cache misses) so a result is reproducible. A "Load file" button starts
// a chosen video from position 0 for a clean run; an optional duration limit
// pauses playback and freezes the results once playback reaches the configured
// length. Frame timing is sampled every frame; the polled process/player stats
// refresh once per second; idle state is pushed via OnMediaEvent.
class Benchmark final : public QObject, public ModuleBase
{
    Q_OBJECT
    Q_PROPERTY(bool open READ IsOpen NOTIFY changed)
    Q_PROPERTY(QString summary READ Summary NOTIFY changed)
    Q_PROPERTY(bool accumulating READ Accumulating NOTIFY changed)
    Q_PROPERTY(bool complete READ Complete NOTIFY changed)

public:
    Benchmark();
    ~Benchmark() override;

    const char* ModuleName() const override
    {
        return "Benchmark";
    }

    void Toggle()
    {
        SetOpen(!open_);
    }

    [[nodiscard]] QString Summary() const;

    [[nodiscard]] bool Accumulating() const
    {
        return accumulating_;
    }

    [[nodiscard]] bool Complete() const
    {
        return complete_;
    }

    Q_INVOKABLE void chooseFile();
    Q_INVOKABLE void resetRun();

    Q_INVOKABLE void close()
    {
        SetOpen(false);
    }

    [[nodiscard]] bool IsOpen() const
    {
        return open_;
    }

    void HandleMediaEvent(const MediaEvent& event) override;

protected:
    std::vector<framelift::Keybind> Keybinds() override;
    void LoadSettings(IModuleSettings& ps) override;
    void SaveSettings(IModuleSettings& ps) override;
    void OnInstall(IModuleContext& ctx) override;

Q_SIGNALS:
    void changed();

private:
    // Flip the open/closed state, gate the frame-timing timer on it (start while
    // open, stop while closed — Benchmark measures only while visible), and notify.
    void SetOpen(bool open);
    void RequestRefresh();
    void StartRun(const char* path); // load `path` from 0 and begin recording
    void ResetStats();

    bool open_ = false;
    std::string toggleBenchmarkKey_ = "F10";

    SysSampler sampler_;
    SysSample sys_;

    // ── Cached player stats ─────────────────────────────────────────────────────
    bool isIdle_ = true;
    std::string hwDec_ = "N/A";
    int64_t dropped_ = 0;
    int64_t mistimed_ = 0;
    int64_t decodeErrors_ = 0;
    int64_t cacheMisses_ = 0;
    int64_t videoW_ = 0;
    int64_t videoH_ = 0;

    // ── Session-constant context (read once on install) ─────────────────────────
    std::string gfxBackend_;   // active graphics backend name ("OpenGL"/"Vulkan")
    bool gpuIsNvidia_ = false; // active adapter is NVIDIA

    // ── Benchmark run state ─────────────────────────────────────────────────────
    Stat frameStat_;                  // UI frame time (ms)
    std::vector<float> frameSamples_; // per-frame times (ms) for percentile/stutter
    double frameMsSmoothed_ = 0.0;    // EMA of frame time for the live fps readout
    Stat cpuStat_;
    Stat memStat_; // bytes
    Stat gpuStat_;
    bool accumulating_ = false; // folding samples into the Stats
    bool complete_ = false;     // duration limit reached — results frozen
    double timePos_ = 0.0;      // latest playback position (seconds)

    // ── Settings ────────────────────────────────────────────────────────────────
    bool limitDuration_ = false;
    float benchmarkDuration_ = 30.f; // seconds
    std::unique_ptr<BenchmarkSettings> settingsPage_;

    std::chrono::steady_clock::time_point lastRefresh_{};
    static constexpr double refreshInterval = 1.0; // seconds
    QTimer* refreshTimer_ = nullptr;
    std::chrono::steady_clock::time_point lastFrameTick_{};

    void ApplySettings(bool limitDuration, float benchmarkDuration);

    friend class BenchmarkSettings;
};

FRAMELIFT_MODULE_ENTRY(
    Benchmark, {
                   .renderOrder = 70,
               }
)
