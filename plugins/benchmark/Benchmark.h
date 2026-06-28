#pragma once

#include <framelift/core.h>

#include "SysStats.h"

#include <QtCore/QObject>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

class QTimer;
class BenchmarkSettings;

// Running min/avg/max aggregate over one sampled metric. Folded once per
// sample while a benchmark run is recording; reset between runs.
struct Stat
{
    double min = 0.0;
    double max = 0.0;
    double sum = 0.0;
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
        ++count;
    }

    [[nodiscard]] double Avg() const
    {
        return count > 0 ? sum / static_cast<double>(count) : 0.0;
    }
};

// Performance benchmark window (F10 to toggle).
// Shows live UI rendering performance — frame rate and frame time with running
// min/avg/max — alongside process performance (CPU %, memory, GPU %) and player
// playback stats (dropped/mistimed frames and the active hardware/software
// decoder), in a movable/resizable window that spawns as its own OS window
// beside the app. A "Load file" button starts a chosen video from position 0 for
// a reproducible run; an optional duration limit pauses playback and freezes the
// results once playback reaches the configured length. Frame timing is sampled
// every frame; the polled process/player stats refresh once per second; idle
// state is pushed via OnMediaEvent.
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

    // ── Benchmark run state ─────────────────────────────────────────────────────
    Stat frameStat_;               // UI frame time (ms)
    double frameMsSmoothed_ = 0.0; // EMA of frame time for the live fps readout
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
