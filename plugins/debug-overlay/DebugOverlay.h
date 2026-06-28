#pragma once

#include <framelift/core.h>

#include <QtCore/QObject>
#include <chrono>
#include <cstdint>
#include <string>

class QTimer;

// Debug statistics overlay (Tab to toggle).
// Displays live player stats — file, title, position, video dims, HwDec, cache,
// dropped/mistimed frames, volume, window size, and playback status — in a
// dark semi-transparent panel anchored to the top-left corner.
// Polled stats refresh once per second; push-observed stats (pause, position)
// update every frame via OnMediaEvent.
class DebugOverlay final : public QObject, public ModuleBase
{
    Q_OBJECT
    Q_PROPERTY(bool open READ IsOpen NOTIFY changed)
    Q_PROPERTY(QString summary READ Summary NOTIFY changed)

public:
    const char* ModuleName() const override
    {
        return "DebugOverlay";
    }

    void Toggle()
    {
        SetOpen(!open_);
    }

    [[nodiscard]] QString Summary() const;

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
    void OnInstall(IModuleContext& ctx) override;

Q_SIGNALS:
    void changed();

private:
    // Flip the open/closed state and gate the 1 s poll timer on it (no polling
    // while the overlay is hidden), then notify.
    void SetOpen(bool open);
    void RequestRefresh();

    bool open_ = false;
    std::string toggleDebugOverlayKey_ = "Tab";

    // ── Cached stats ───────────────────────────────────────────────────────────
    std::string filePath_;
    std::string title_;
    std::string hwDec_ = "N/A";
    std::string gfxBackend_; // active graphics backend name, queried once (session-constant)
    double timePos_ = 0.0;
    double duration_ = 0.0;
    bool isPaused_ = false;
    bool isIdle_ = true;
    int64_t videoW_ = 0;
    int64_t videoH_ = 0;
    double volume_ = 100.0;
    int64_t dropped_ = 0;
    int64_t mistimed_ = 0;
    int64_t decodeErrors_ = 0;
    int64_t cacheUsed_ = 0;
    int64_t cacheHits_ = 0;
    int64_t cacheMisses_ = 0;

    std::chrono::steady_clock::time_point lastRefresh_{};
    static constexpr double refreshInterval = 1.0; // seconds
    QTimer* refreshTimer_ = nullptr;
};

FRAMELIFT_MODULE_ENTRY(
    DebugOverlay, {
                      .renderOrder = 60,
                  }
)
