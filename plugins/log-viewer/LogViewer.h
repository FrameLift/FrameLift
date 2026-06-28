#pragma once

#include <framelift/core.h>
#include <framelift/services.h>

#include <QtCore/QObject>
#include <QtCore/QVariantList>
#include <deque>
#include <memory>
#include <string>
#include <vector>

class QTimer;
class LogViewerSettings;

// In-app log viewer (Ctrl+L to toggle). Reads recent log lines back from the
// host's in-memory ring buffer via the ILogBuffer service and shows them in a
// scrolling, filterable window. Performance measurements are emitted at
// Log::Level::Perf; the "Perf only" toggle isolates them.
class LogViewer final : public QObject, public ModuleBase
{
    Q_OBJECT
    Q_PROPERTY(bool open READ IsOpen NOTIFY changed)
    Q_PROPERTY(QVariantList lines READ QmlLines NOTIFY changed)
    Q_PROPERTY(QString filterText READ FilterText WRITE SetFilterText NOTIFY changed)
    Q_PROPERTY(bool perfOnly READ PerfOnly WRITE SetPerfOnly NOTIFY changed)

public:
    const char* ModuleName() const override
    {
        return "LogViewer";
    }

    void Toggle()
    {
        SetOpen(!open_);
    }

    [[nodiscard]] bool IsOpen() const
    {
        return open_;
    }

    [[nodiscard]] QVariantList QmlLines() const;

    [[nodiscard]] QString FilterText() const
    {
        return QString::fromStdString(filterText_);
    }

    void SetFilterText(const QString& value)
    {
        filterText_ = value.toStdString();
        Q_EMIT changed();
    }

    [[nodiscard]] bool PerfOnly() const
    {
        return perfOnly_;
    }

    void SetPerfOnly(bool value)
    {
        perfOnly_ = value;
        Q_EMIT changed();
    }

    Q_INVOKABLE void clearLines();

    Q_INVOKABLE void close()
    {
        SetOpen(false);
    }

protected:
    std::vector<framelift::Keybind> Keybinds() override;
    void LoadSettings(IModuleSettings& ps) override;
    void SaveSettings(IModuleSettings& ps) override;
    void OnInstall(IModuleContext& ctx) override;

Q_SIGNALS:
    void changed();

private:
    struct Entry
    {
        unsigned long long seq = 0;
        long long tsMillis = 0;
        int level = 0;
        std::string msg;
    };

    // Flip the open/closed state and gate the 250 ms poll timer on it (no draining
    // the ring buffer while hidden); pulls immediately on open, then notifies.
    void SetOpen(bool open);
    // ILogBuffer::Visitor trampoline — `ud` is the LogViewer instance.
    static void OnEntry(void* ud, unsigned long long seq, long long tsMillis, int level, const char* msg);
    // Drain newly-appended lines from the ring buffer; returns true iff ≥1 entry was added.
    bool Pull();
    [[nodiscard]] bool Passes(const Entry& e) const;

    ILogBuffer* logs_ = nullptr;
    unsigned long long lastSeq_ = 0;
    std::deque<Entry> entries_;
    static constexpr std::size_t kMaxEntries = 5000;

    bool open_ = false;
    std::string toggleKey_ = "Ctrl+L";

    // Persisted filter state.
    bool showDebug_ = true;
    bool showInfo_ = true;
    bool showWarn_ = true;
    bool showError_ = true;
    bool perfOnly_ = false;
    std::unique_ptr<LogViewerSettings> settingsPage_;

    // Runtime-only text search (not persisted).
    std::string filterText_;
    QTimer* refreshTimer_ = nullptr;

    // ── QML lines cache ────────────────────────────────────────────────────────
    // QmlLines() filters and rebuilds a QVariantList on every read; with the ring
    // buffer holding up to kMaxEntries lines this is costly. Cache it and
    // invalidate whenever changed fires (the NOTIFY for `lines` and the filters).
    mutable QVariantList linesCache_;
    mutable bool linesCacheDirty_ = true;

    void ApplySettings(bool showDebug, bool showInfo, bool showWarn, bool showError, bool perfOnly);

    friend class LogViewerSettings;
};

FRAMELIFT_MODULE_ENTRY(
    LogViewer, {
                   .renderOrder = 70,
               }
)
