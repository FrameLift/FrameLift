#pragma once

#include <framelift/core.h>
#include <framelift/platform.h>
#include <framelift/services.h>

#include <QtCore/QObject>
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
class RemoteStream final : public QObject, public ModuleBase
{
    Q_OBJECT
    Q_PROPERTY(bool dialogOpen READ DialogOpen NOTIFY dialogChanged)
    Q_PROPERTY(QString url READ Url WRITE SetUrl NOTIFY dialogChanged)

public:
    RemoteStream() = default;

    // Resolve a remote URL into a locator the media player (FFmpeg) can read.
    // Returns "" on failure. Public so it can be unit-tested; this is the
    // replaceable custom-decryption hook.
    [[nodiscard]] std::string ResolveStream(const std::string& url) noexcept;

    [[nodiscard]] bool DialogOpen() const
    {
        return dialogOpen_;
    }

    [[nodiscard]] QString Url() const
    {
        return QString::fromStdString(urlInput_);
    }

    void SetUrl(const QString& value)
    {
        urlInput_ = value.toStdString();
        Q_EMIT dialogChanged();
    }

    Q_INVOKABLE void submit();
    Q_INVOKABLE void cancel();

protected:
    const char* ModuleName() const override
    {
        return "RemoteStream";
    }

    void OnInstall(IModuleContext& ctx) override;

Q_SIGNALS:
    void dialogChanged();

private:
    // Resolve `url`, hand the result to the media player, then update the window
    // title and history. Invoked from the OpenFileRequestEvent subscription.
    void OpenUrl(const std::string& url) noexcept;

    bool dialogOpen_ = false; // true while the URL-entry modal is on screen
    std::string urlInput_;    // URL text-field contents
};

FRAMELIFT_MODULE_ENTRY(
    RemoteStream, {
                      .renderOrder = 100, // draw the modal above the panels
                  }
)
