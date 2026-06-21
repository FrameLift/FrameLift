#pragma once

#include <framelift/core.h>
#include <framelift/services.h>
#include <framelift/ui.h>

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Slide-in panel (right edge) showing recently played files with resume positions.
// Entries are persisted to a plain-text file in the user's pref directory.
class History : public Panel, public ModuleBase, public IHistory
{
public:
    History();

    // ── IModule ───────────────────────────────────────────────────────────────
    bool HandleKeyDownEvent(const AppEvent& e) override;

    // Set the file path for persistence and immediately load any saved data.
    // Must be called before AddEntry(). Replaces the old SDL_GetPrefPath logic.
    void SetStoragePath(std::string path);

    // Push a path to the front; deduplicates, caps, and persists.
    // Driven internally by the FileOpenedEvent subscription.
    void AddEntry(const char* path) noexcept;

    // Erase all entries and persist the empty list.
    void Clear();

    // IHistory
    int GetMostRecent(char* buf, int cap) const noexcept override;

    // Update the saved resume position. No-op if path not found.
    // Driven internally by the FileEndedEvent subscription.
    void UpdateResumePos(const char* path, double pos) noexcept;

    // IHistory: return the saved resume position for `path`, or 0.0 if not found.
    [[nodiscard]] double GetResumePos(const char* path) const noexcept override;

    // Serialise entries to storagePath_.
    void Save() const noexcept;

    // ── Keyboard navigation ────────────────────────────────────────────────────
    void CursorUp();
    void CursorDown();
    // Open the file under the keyboard cursor via the registered load callback.
    void ConfirmCursor() const;

protected:
    // ── ModuleBase hooks ────────────────────────────────────────────────────
    const char* ModuleName() const override
    {
        return "History";
    }

    std::vector<framelift::SettingsField> SettingsFields() override;
    std::vector<framelift::Keybind> Keybinds() override;
    void OnInstall(IModuleContext& ctx) override;
    void RenderSettings(UIContext& ctx) override;

    // Reset cursor to the top entry when the panel opens.
    void OnOpened() override
    {
        cursor_ = filteredIndices_.empty() ? -1 : 0;
    }

    void RenderContent(float panelW, float panelH, UIContext& ctx) override;

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
    // Rebuild filteredIndices_ from entries_ using searchQuery_; clamps cursor_.
    void RebuildFilter();

    // ── Plugin-owned settings ─────────────────────────────────────────────────
    int maxEntries_ = 200;
    std::string toggleHistoryKey_ = "H";

    std::deque<Entry> entries_;
    std::vector<int> filteredIndices_; // indices into entries_ matching searchQuery_
    std::string searchQuery_;
    int cursor_ = -1; // keyboard-navigation cursor into filteredIndices_, or -1 if none
    std::string storagePath_;

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
};

FRAMELIFT_MODULE_ENTRY(History, {
    .renderOrder = 20,
})
