#pragma once

// ── Application-level pub/sub events ─────────────────────────────────────────
// All fields are POD — no heap allocation, safe across DLL boundaries.
// const char* fields are valid only during the subscriber callback (synchronous).
// Subscribers that need to outlive the call must copy into their own storage.

// Any plugin can publish to notify the user; Overlay subscribes and shows it.
struct NotificationEvent
{
    static constexpr const char* EventId = "framelift.NotificationEvent";
    const char* text = nullptr;
};

// Published by Playlist when a file begins playing.
struct FileOpenedEvent
{
    static constexpr const char* EventId = "framelift.FileOpenedEvent";
    const char* path = nullptr;
};

// Published by Playlist when a file ends or its position is flushed on shutdown.
struct FileEndedEvent
{
    static constexpr const char* EventId = "framelift.FileEndedEvent";
    const char* path = nullptr;
    double position = 0.0;
};

// Request to open a file for playback. Published by History (entry selected),
// the host (file drop, open dialog, resume-last), or any other plugin.
// Playlist (or any other handler) subscribes to open the file.
struct OpenFileRequestEvent
{
    static constexpr const char* EventId = "framelift.OpenFileRequestEvent";
    const char* path = nullptr;
    bool rebuildPlaylist = false; // true = rescan the directory and rebuild; false = just play this file
};

// Request to prompt the user for a remote stream URL. Published by the host's
// "Open Network Stream…" context-menu item; the RemoteStream plugin subscribes
// and opens its URL-entry modal. No payload — it is purely a "show the prompt".
struct OpenNetworkStreamRequestEvent
{
    static constexpr const char* EventId = "framelift.OpenNetworkStreamRequestEvent";
};

// Published by side drawers whenever their animated visible width changes; consumers
// (e.g. Overlay's controls-bar inset) cache the latest value per side.
struct PanelLayoutEvent
{
    static constexpr const char* EventId = "framelift.PanelLayoutEvent";
    int side = 0; // 0 = left, 1 = right
    float visibleWidth = 0.f;
};

// Published by SettingsMenu when the dialog opens/closes; Overlay hides the
// HUD while it is open.
struct SettingsVisibilityEvent
{
    static constexpr const char* EventId = "framelift.SettingsVisibilityEvent";
    bool open = false;
};

// Published once by the host at startup (after all plugins are loaded and
// subscribed) carrying the process command line verbatim. The host itself only
// consumes the first positional argument as a file/URL to open; this event lets
// any plugin interpret the remaining arguments — subcommands, flags, batch or
// analysis tasks — without the host knowing about them. `argv[0]` is the program
// name. The argv array originates from main() and stays valid for the process
// lifetime, but copy any strings you retain to keep with the POD-lifetime rule.
struct CliCommandEvent
{
    static constexpr const char* EventId = "framelift.CliCommandEvent";
    int argc = 0;
    const char* const* argv = nullptr;
};
