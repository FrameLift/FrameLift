#pragma once

#include <framelift/core.h>
#include <framelift/services.h>

#include <QtCore/QObject>
#include <QtCore/QVariantList>
#include <deque>
#include <memory>
#include <string>
#include <vector>

// Slide-in panel (right edge) showing recently played files with resume positions.
// Entries are persisted to the shared media store (SQLite) under the `history`
// namespace; every mutation writes through synchronously.
class HistorySettings;

class History : public QObject, public ModuleBase, public IHistory
{
    Q_OBJECT
    Q_PROPERTY(bool open READ IsOpen NOTIFY panelStateChanged)
    Q_PROPERTY(QString search READ Search WRITE SetSearch NOTIFY historyChanged)
    Q_PROPERTY(QVariantList entries READ QmlEntries NOTIFY historyChanged)

public:
    History();
    ~History() override;

    // ── IModule ───────────────────────────────────────────────────────────────
    bool HandleKeyDownEvent(const AppEvent& e) override;

    // Inject the host media store used for persistence and immediately create the
    // history schema and load any saved entries. Set from OnInstall in production;
    // tests inject a MediaStoreImpl over a temp database directly. A null/absent
    // store degrades to in-memory-only history.
    void SetMediaStore(IMediaStore* store);

    // Push a path to the front; deduplicates, caps, and persists.
    // Driven internally by the FileOpenedEvent subscription.
    void AddEntry(const char* path) noexcept;

    // Erase all entries and persist the empty list.
    Q_INVOKABLE void Clear();

    [[nodiscard]] QString Search() const
    {
        return QString::fromStdString(searchQuery_);
    }

    void SetSearch(const QString& value);
    [[nodiscard]] QVariantList QmlEntries() const;
    Q_INVOKABLE void togglePanel();
    Q_INVOKABLE void activateIndex(int filteredIndex);
    Q_INVOKABLE void publishVisibleWidth(qreal width);

    [[nodiscard]] bool IsOpen() const
    {
        return open_;
    }

    // IHistory
    int GetMostRecent(char* buf, int cap) const noexcept override;

    // Update the saved resume position. No-op if path not found.
    // Driven internally by the FileEndedEvent subscription.
    void UpdateResumePos(const char* path, double pos) noexcept;

    // IHistory: return the saved resume position for `path`, or 0.0 if not found.
    [[nodiscard]] double GetResumePos(const char* path) const noexcept override;

protected:
    // ── ModuleBase hooks ────────────────────────────────────────────────────
    const char* ModuleName() const override
    {
        return "History";
    }

    std::vector<framelift::Keybind> Keybinds() override;
    void LoadSettings(IModuleSettings& ps) override;
    void SaveSettings(IModuleSettings& ps) override;
    void OnInstall(IModuleContext& ctx) override;

Q_SIGNALS:
    void historyChanged();
    void panelStateChanged();

private:
    struct Entry
    {
        std::string path;         // column: path (PRIMARY KEY)
        std::string label;        // display name (filename without directory) — not persisted
        double resumePos = 0.0;   // column: resume_pos — last known playback position in seconds
        std::string playbackDate; // derived from column last_played_utc — local "%Y-%m-%d %H:%M:%S"
        // Cached display strings, recomputed only on mutation (not per frame) — the
        // panel renders every frame, so per-row path parsing/formatting would be hot.
        std::string dir;  // parent directory of path
        std::string meta; // "<playbackDate>  ·  <resume position>"
    };

    // Extract the filename component of a path for use as a display label.
    static std::string FilenameOf(const std::string& path);
    // Refresh an entry's cached display strings (dir, meta) from its path/pos/date.
    static void FormatEntry(Entry& e);
    // Create/migrate the history_* tables (per-namespace schema versioning).
    void EnsureSchema();
    // Rebuild entries_ from the store; called from SetMediaStore().
    void Load();
    // Write-through helpers; each is a no-op without a store.
    void PersistEntry(const char* path, double resumePos, long long playedUtc) const;
    void PersistResumePos(const char* path, double pos) const;
    void PersistTrim() const;
    void PersistClear() const;
    // Maximum number of entries to retain, sourced from settings (or a fallback).
    [[nodiscard]] int MaxEntries() const;
    // Rebuild filteredIndices_ from entries_ using searchQuery_.
    void RebuildFilter();

    // ── Plugin-owned settings ─────────────────────────────────────────────────
    int maxEntries_ = 200;
    std::string toggleHistoryKey_ = "H";
    bool open_ = false;
    std::unique_ptr<HistorySettings> settingsPage_;

    std::deque<Entry> entries_;
    std::vector<int> filteredIndices_; // indices into entries_ matching searchQuery_
    std::string searchQuery_;
    IMediaStore* store_ = nullptr; // shared media store (host-owned; not owned here)

    void ApplySettings(int maxEntries);
    void SetOpen(bool value);

    // ── QML entries cache ──────────────────────────────────────────────────────
    // QmlEntries() is read once per delegate realization; rebuilding the whole
    // QVariantList each time is wasteful. Cache it and invalidate whenever
    // historyChanged fires (the NOTIFY for the `entries` and `search` properties).
    mutable QVariantList entriesCache_;
    mutable bool entriesCacheDirty_ = true;

    friend class HistorySettings;
};

FRAMELIFT_MODULE_ENTRY(
    History, {
                 .renderOrder = 20,
             }
)
