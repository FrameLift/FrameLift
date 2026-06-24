#pragma once

class HotkeysImpl;
class Settings;
class FFmpegPlayer;
class IAppWindow;
class IGraphicsSurface;
class IEventPump;
class IFileDialog;
class IModuleContext;

// Owns the host's playback/app hotkeys and their actions (pause, fullscreen, quit,
// normalize, volume, mute, subtitles, seek, open-file). Extracted from App so the
// host object stays focused on lifecycle, the event loop, and rendering.
//
// Talks to the player/window/file-dialog through their interfaces and publishes
// user-facing notifications via the module context — it owns no rendering and no
// playback state beyond a mirror of the player's idle flag.
class PlaybackControls
{
public:
    PlaybackControls(
        HotkeysImpl& keys, const Settings& settings, FFmpegPlayer& player, IAppWindow& window,
        IGraphicsSurface& gfx, IEventPump& events, IFileDialog& fileDialog, IModuleContext& ctx
    );

    // Apply the player+window settings now, then keep them in sync: subscribe to
    // settings changes (re-apply), to NotificationEvent (duck audio), and observe
    // the player idle property. Call once during host init.
    void Connect();

    // Register every key binding. Call once, after the module registry binds its keys.
    void Bind();

    // Mirror of the player idle state (no file loaded), fed from the host's media
    // event drain so TogglePause can resume the most recent file when idle.
    void SetPlayerIdle(bool idle) noexcept
    {
        playerIdle_ = idle;
    }

private:
    void TogglePause() const;
    void AdjustVolumeAndNotify(int delta) const;
    void OpenFileDialog() const;

    HotkeysImpl& keys_;
    const Settings& settings_;
    FFmpegPlayer& player_;
    IAppWindow& window_;
    IGraphicsSurface& gfx_;
    IEventPump& events_;
    IFileDialog& fileDialog_;
    IModuleContext& ctx_;
    bool playerIdle_ = true;
};
