#pragma once

#include <framelift/core.h>
#include <framelift/platform.h>
#include <framelift/services.h>
#include <framelift/ui.h>

#include <string>

// Adds playback of remote network streams. This plugin is the sole handler of
// remote URLs — Playlist ignores anything framelift::IsRemoteUrl() recognises.
//
//  • Plain schemes (http/https/rtsp/rtmp/...) are handed to the media player
//    unchanged; FFmpeg's built-in protocol handlers read them directly.
//  • The dedicated "flsec://" scheme is the custom-encryption path: ResolveStream()
//    turns it into something the player can read (the reference build decrypts to a
//    local temp file). Replace this plugin with your own DLL to implement a real
//    fetch + decryption scheme — nothing else in the app needs to change.
//
// UI: an "Open Network Stream…" context-menu entry opens a modal with a URL field.
class RemoteStream final : public SafeRenderable, public PluginBase
{
public:
    RemoteStream() = default;

    // Resolve a remote URL into a locator the media player (FFmpeg) can read.
    // Returns "" on failure. Public so it can be unit-tested; this is the
    // replaceable custom-decryption hook.
    [[nodiscard]] std::string ResolveStream(const std::string& url) noexcept;

    // ── IRenderable (via SafeRenderable) ──────────────────────────────────────
    void OnRender(int windowW, int windowH, UIContext& ctx) override;
    [[nodiscard]] bool RedrawNeeded() const override;

protected:
    const char* PluginName() const override
    {
        return "RemoteStream";
    }

    void OnInstall(IPluginContext& ctx) override;

private:
    // Resolve `url`, hand the result to the media player, then update the window
    // title and history. Invoked from the OpenFileRequestEvent subscription.
    void OpenUrl(const std::string& url) noexcept;

    bool requestOpen_ = false; // set by the menu item; triggers OpenPopup next frame
    bool modalOpen_ = false;   // true while the modal is on screen
    std::string urlInput_;     // URL text-field contents
};
