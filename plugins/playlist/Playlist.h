#pragma once

#include <framelift/core.h>
#include <framelift/platform.h>
#include <framelift/services.h>

#include <QtCore/QFileSystemWatcher>
#include <QtCore/QObject>
#include <QtCore/QVariantList>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Slide-in panel (left edge) that lists and navigates the files in the current
// directory. Automatically populated when a file is opened via OpenFile().
// Driven via OpenFileRequestEvent — there is no service interface.
class PlaylistSettings;

class Playlist : public QObject, public ModuleBase
{
    Q_OBJECT
    Q_PROPERTY(bool open READ IsOpen NOTIFY panelStateChanged)
    Q_PROPERTY(bool shuffleEnabled READ IsShuffleEnabled NOTIFY playlistChanged)
    Q_PROPERTY(bool sortByName READ IsSortByName NOTIFY playlistChanged)
    Q_PROPERTY(int currentIndex READ Current NOTIFY playlistChanged)
    Q_PROPERTY(QVariantList entries READ QmlEntries NOTIFY playlistChanged)

public:
    Playlist();
    ~Playlist();

    // ── IModule ───────────────────────────────────────────────
    bool HandleEvent(const AppEvent& e) override;
    bool HandleKeyDownEvent(const AppEvent& e) override;
    void HandleMediaEvent(const MediaEvent& e) override;

    // Open a file by scanning its directory, populating the playlist, activating it.
    void OpenFile(const char* path) noexcept;
    // Load a file for playback without rebuilding the playlist.
    void LoadFile(const char* path) noexcept;

    // ── Entries ────────────────────────────────────────────────────────────────
    // Append a single path (does not activate it). `subfolder` is the directory
    // relative to the scanned root, leading-slash ("/" at the root).
    void AddFile(std::string path, std::string subfolder);
    // Remove all entries and reset current_ to -1.
    void Clear();

    [[nodiscard]] bool Empty() const
    {
        return entries_.empty();
    }

    [[nodiscard]] int Count() const
    {
        return static_cast<int>(entries_.size());
    }

    [[nodiscard]] int Current() const
    {
        return current_;
    }

    [[nodiscard]] QVariantList QmlEntries() const;

    Q_INVOKABLE void togglePanel();
    Q_INVOKABLE void activateIndex(int index);
    Q_INVOKABLE void publishVisibleWidth(qreal width);

    // Advance to the next entry and activate it (wraps to the beginning).
    void Next();
    // Retreat to the previous entry and activate it (wraps to the end).
    void Prev();
    // Activate the last entry in the list.
    void ActivateLast();
    // Find an entry whose path matches and activate it; no-op if not found.
    void ActivateByPath(const std::string& path);

    // Persist the current file's playback position to history without loading
    // a new file. Call this before the application exits.
    void FlushCurrentPos() const;

    // ── Reload / Shuffle ────────────────────────────────────────────
    // Re-scan the watched directory and update entries without interrupting playback.
    Q_INVOKABLE void Reload();
    // Toggle shuffle mode; rebuilds the order when enabling.
    Q_INVOKABLE void ToggleShuffle();
    // Toggle the sort mode for the current session (re-sorts in place, no rescan).
    // A temporary override of the persisted "sort by name" default setting.
    Q_INVOKABLE void toggleSortByName();

    [[nodiscard]] bool IsShuffleEnabled() const
    {
        return shuffleEnabled_;
    }

    [[nodiscard]] bool IsSortByName() const
    {
        return sortByNameActive_;
    }

    [[nodiscard]] bool IsOpen() const
    {
        return open_;
    }

    void ApplySettings(
        bool scanSubdirs, int scanMaxDepth, bool mixedPlaylist, bool imageSlideshow, float slideshowDuration,
        bool autoReload, bool sortByName
    );

protected:
    // ── ModuleBase hooks ────────────────────────────────────────────────────
    const char* ModuleName() const override
    {
        return "Playlist";
    }

    std::vector<framelift::Keybind> Keybinds() override;
    void LoadSettings(IModuleSettings& ps) override;
    void SaveSettings(IModuleSettings& ps) override;
    void OnInstall(IModuleContext& ctx) override;

Q_SIGNALS:
    void playlistChanged();
    void panelStateChanged();

private:
    struct Entry
    {
        std::string path;
        std::string label;     // display name (filename without directory)
        std::string subfolder; // directory relative to the scanned root, leading-slash ("/" at the root)
    };

    // Set current_ to index and trigger onLoad_ to begin playback.
    void Activate(int index);
    // Extract the filename component of a path for use as a display label.
    static std::string FilenameOf(const std::string& path);
    // Subfolder of `path` relative to `base` (forward slashes), empty if directly
    // in `base` or if `base` is empty.
    static std::string SubfolderOf(const std::string& path, const std::string& base);

    // Replace entries_ with the (to-be-sorted) scanned paths, keeping keepPath
    // present and selected (current_) without restarting playback.
    // Subfolder labels are computed relative to `baseDir` (the scanned root).
    // Shared by Reload() and the async-scan completion path.
    void RebuildEntries(std::vector<std::string>& files, const std::string& keepPath, const std::string& baseDir);
    // UI-thread handler for a completed background directory scan: swaps in the
    // full playlist and (re)arms the directory watcher. No-op if superseded.
    void ApplyScanResult();

    std::vector<Entry> entries_;
    int current_ = -1; // index of the currently playing entry, or -1 if none

    // ── Header counter cache ──────────────────────────────────────────────────
    // "<current+1> / <total>", rebuilt only when current_ or the entry count
    // changes — the panel renders every frame, so this avoids per-frame allocs.
    std::string counterText_;
    int counterCur_ = -2;          // current_ the cache was built for (-2 = unset)
    std::size_t counterTotal_ = 0; // entries_.size() the cache was built for

    // ── Shuffle ─────────────────────────────────────────────────────
    bool shuffleEnabled_ = false;
    std::vector<Entry> sortedEntries_; // sorted backup; populated when shuffle is on
    void ApplyShuffleToEntries();      // shuffles entries_ in-place, current_ stays valid

    // ── Sort order ─────────────────────────────────────────────────
    // sortByName_ is the persisted default; sortByNameActive_ is the live mode
    // (seeded from the default, flipped temporarily via the panel toggle). When
    // true entries are ordered by filename (case-insensitive) so files interleave
    // across subfolders; otherwise by full path so subfolders stay grouped.
    bool sortByNameActive_ = false;
    // Order entries_ (or sortedEntries_ when shuffle is on) by the active mode.
    void SortEntries(std::vector<Entry>& entries) const;
    // Re-apply the active sort to the live list in place, keeping current_.
    void Resort();

    // ── Directory watching ─────────────────────────────────────────────
    std::string watchedDir_;
    uint32_t dirChangedEventType_ = 0;
    QFileSystemWatcher dirWatcher_;
    void ArmDirectoryWatcher();
    void ClearDirectoryWatcher();

    // ── Async directory scan ───────────────────────────────────────────────────
    // OpenFile() starts playback immediately and offloads the recursive directory
    // scan to a detached worker so a large/nested folder never blocks the UI thread
    // or the start of playback. The worker fills this heap-owned slot (it outlives
    // the plugin via shared_ptr) and wakes the UI thread with a custom event; the
    // UI thread alone mutates entries_, so no locking of entries_ is needed.
    uint32_t scanDoneEventType_ = 0;

    struct ScanShared
    {
        std::mutex mtx;
        std::vector<std::string> files;     // scanned paths (guarded by mtx)
        std::string dir;                    // directory that was scanned
        std::string openedPath;             // file that triggered this scan
        uint64_t gen = 0;                   // generation of the published result
        std::atomic<uint64_t> latestGen{0}; // newest OpenFile() generation
        bool ready = false;                 // a result is waiting to be applied
        std::atomic<bool> alive{true};      // cleared in ~Playlist
        IEventPump* events = nullptr;       // host service, app-lifetime
        uint32_t doneEventType = 0;
    };

    std::shared_ptr<ScanShared> scanShared_ = std::make_shared<ScanShared>();
    // Build scan parameters from settings and launch the worker (UI thread).
    void StartScan(const std::string& path);

    std::string currentFile_;
    double currentTimePos_ = 0.0; // most recent time-pos for the current file

    // ── Plugin-owned settings ─────────────────────────────────────────────────
    bool scanSubdirs_ = true;
    int scanMaxDepth_ = 5;
    bool mixedPlaylist_ = false;
    bool imageSlideshow_ = false;
    float slideshowDuration_ = 5.0f;
    bool autoReload_ = true;
    bool sortByName_ = false;
    bool open_ = false;
    std::unique_ptr<PlaylistSettings> settingsPage_;

    std::string togglePlaylistKey_ = "L";
    std::string nextTrackKey_ = "Ctrl+Right";
    std::string prevTrackKey_ = "Ctrl+Left";
    std::string reloadPlaylistKey_ = "Ctrl+R";
    std::string toggleShuffleKey_ = "Shift+S";

    void SetOpen(bool value);

    // ── QML entries cache ──────────────────────────────────────────────────────
    // QmlEntries() is read once per delegate realization; rebuilding the whole
    // QVariantList each time is costly for long playlists. Cache it and invalidate
    // whenever playlistChanged fires (the NOTIFY for the `entries` property).
    mutable QVariantList entriesCache_;
    mutable bool entriesCacheDirty_ = true;

    friend class PlaylistSettings;
};

FRAMELIFT_MODULE_ENTRY(
    Playlist, {
                  .renderOrder = 10,
              }
)
