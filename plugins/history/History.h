#pragma once

#include <framelift/core.h>
#include <framelift/services.h>

#include <QtCore/QObject>
#include <QtCore/QVariantList>
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Slide-in panel (right edge) showing recently played files with resume positions.
// Entries are persisted to a plain-text file in the user's pref directory.
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

    // Inject the host JSON service used for load/save. Set from OnInstall in
    // production; tests inject a JsonServiceImpl directly. Must be set before
    // SetStoragePath() for Load() to read anything.
    void SetJsonService(IJson* json) noexcept
    {
        json_ = json;
    }

    // Set the file path for persistence and immediately load any saved data.
    // Must be called before AddEntry().
    void SetStoragePath(std::string path);

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

    // Serialise entries to storagePath_.
    void Save() const noexcept;

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
        std::string path;         // JSON: "p"
        std::string label;        // display name (filename without directory) — not persisted
        double resumePos = 0.0;   // JSON: "r" — last known playback position in seconds
        std::string playbackDate; // JSON: "d" — ISO 8601 local timestamp of last play
        // Cached display strings, recomputed only on mutation (not per frame) — the
        // panel renders every frame, so per-row path parsing/formatting would be hot.
        std::string dir;  // parent directory of path
        std::string meta; // "<playbackDate>  ·  <resume position>"
    };

    // Extract the filename component of a path for use as a display label.
    static std::string FilenameOf(const std::string& path);
    // Refresh an entry's cached display strings (dir, meta) from its path/pos/date.
    static void FormatEntry(Entry& e);
    // Deserialise entries from storagePath_; called from SetStoragePath().
    void Load();
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
    std::string storagePath_;
    IJson* json_ = nullptr; // host JSON service for load/save (host-owned; not owned here)

    // ── Save ordering ─────────────────────────────────────────────────────────
    // Each Save() snapshots and writes on a detached thread, so renames can land
    // out of order. We stamp every save with a monotonic sequence and let the
    // background writer commit (rename) only while holding `mutex`, discarding any
    // snapshot older than one already published — so the newest entry always wins.
    // Held by shared_ptr captured into the writer thread; outlives `this` safely.
    struct SaveCoordinator
    {
        std::mutex mutex;
        unsigned published = 0; // highest seq already renamed into place
        bool any = false;       // whether `published` is meaningful yet
    };

    mutable std::atomic<unsigned> saveSeq_{0};
    std::shared_ptr<SaveCoordinator> saveCoord_ = std::make_shared<SaveCoordinator>();

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
