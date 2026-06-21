#pragma once

#include <framelift/core.h>
#include <framelift/platform.h>
#include <framelift/services.h>
#include <framelift/ui.h>

#include <string>
#include <vector>

// Slide-in panel (left edge) that lists and navigates the files in the current
// directory. Automatically populated when a file is opened via OpenFile().
// Driven via OpenFileRequestEvent — there is no service interface.
class Playlist : public Panel, public ModuleBase
{
public:
    Playlist() : Panel(Side::Left, 320.f, "Playlist")
    {
    }

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
    // Append a single path (does not activate it).
    void AddFile(std::string path);
    // Append multiple paths at once (does not activate any of them).
    void AddFiles(const std::vector<std::string>& paths);
    // Remove all entries and reset current_ and cursor_ to -1.
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
    void Reload();
    // Toggle shuffle mode; rebuilds the order when enabling.
    void ToggleShuffle();

    [[nodiscard]] bool IsShuffleEnabled() const
    {
        return shuffleEnabled_;
    }

    // ── Keyboard navigation (called when panel is open) ────────────────────────
    void CursorUp();
    void CursorDown();
    // Load the file under the keyboard cursor.
    void ConfirmCursor();

protected:
    // ── ModuleBase hooks ────────────────────────────────────────────────────
    const char* ModuleName() const override
    {
        return "Playlist";
    }

    std::vector<framelift::SettingsField> SettingsFields() override;
    std::vector<framelift::Keybind> Keybinds() override;
    void OnInstall(IModuleContext& ctx) override;
    void RenderSettings(UIContext& ctx) override;

    // Reset cursor to the currently playing entry when the panel opens.
    void OnOpened() override;
    void RenderContent(float panelW, float panelH, UIContext& ctx) override;

private:
    struct Entry
    {
        std::string path;
        std::string label; // display name (filename without directory)
    };

    // Set current_ to index and trigger onLoad_ to begin playback.
    void Activate(int index);
    // Extract the filename component of a path for use as a display label.
    static std::string FilenameOf(const std::string& path);

    std::vector<Entry> entries_;
    int current_ = -1; // index of the currently playing entry, or -1 if none
    int cursor_ = -1;  // keyboard-navigation cursor, or -1 if none

    // ── Header counter cache ──────────────────────────────────────────────────
    // "<current+1> / <total>", rebuilt only when current_ or the entry count
    // changes — the panel renders every frame, so this avoids per-frame allocs.
    std::string counterText_;
    int counterCur_ = -2;             // current_ the cache was built for (-2 = unset)
    std::size_t counterTotal_ = 0;    // entries_.size() the cache was built for

    // ── Shuffle ─────────────────────────────────────────────────────
    bool shuffleEnabled_ = false;
    std::vector<Entry> sortedEntries_; // sorted backup; populated when shuffle is on
    void ApplyShuffleToEntries();      // shuffles entries_ in-place, current_ stays valid

    // ── Directory watching ─────────────────────────────────────────────
    std::string watchedDir_;
    uint32_t dirChangedEventType_ = 0;

    std::string currentFile_;
    double currentTimePos_ = 0.0; // most recent time-pos for the current file

    // ── Plugin-owned settings ─────────────────────────────────────────────────
    bool scanSubdirs_ = true;
    int scanMaxDepth_ = 5;
    bool mixedPlaylist_ = false;
    bool imageSlideshow_ = false;
    float slideshowDuration_ = 5.0f;
    bool autoReload_ = true;

    std::string togglePlaylistKey_ = "L";
    std::string nextTrackKey_ = "Ctrl+Right";
    std::string prevTrackKey_ = "Ctrl+Left";
    std::string reloadPlaylistKey_ = "Ctrl+R";
    std::string toggleShuffleKey_ = "Shift+S";

    // Held across Watch() calls — must outlive the watcher callback.
    struct WatchCbCtx
    {
        Playlist* self;
        IEventPump* events;
    } watchCbCtx_{};

    void RenderSettingsContent(UIContext& ctx);
};

FRAMELIFT_MODULE_ENTRY(Playlist, {
    .renderOrder = 10,
})
