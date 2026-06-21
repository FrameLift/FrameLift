#include "PlaybackControls.h"

#include "CoreSettings.h"          // KeybindSettings
#include "FFmpegPlayer.h"          // ApplySettings + PulseDucking
#include "FFmpegSettingsMapping.h" // ToAudioNormalizeParams + AudioSettings
#include "HotkeysImpl.h"           // host::Bind
#include "PlaybackSettings.h"      // videoSync
#include "Settings.h"

#include <framelift/ContextHelpers.h>
#include <framelift/Events.h>
#include <framelift/IModuleContext.h>
#include <framelift/platform/IAppWindow.h>
#include <framelift/platform/IFileDialog.h>
#include <framelift/platform/IMediaPlayer.h>
#include <framelift/services/IHistory.h>

#include <filesystem>
#include <string>

namespace
{
// Owner cell for the async volume read: heap-allocated, passed as the opaque
// user-data pointer, deleted inside the trampoline once it fires.
struct VolCell
{
    IModuleContext* ctx;
};
} // namespace

PlaybackControls::PlaybackControls(
    HotkeysImpl& keys, const Settings& settings, FFmpegPlayer& player, IAppWindow& window, IGraphicsSurface& gfx,
    IEventPump& events, IFileDialog& fileDialog, IModuleContext& ctx
)
    : keys_(keys), settings_(settings), player_(player), window_(window), gfx_(gfx), events_(events),
      fileDialog_(fileDialog), ctx_(ctx)
{
}

void PlaybackControls::Connect()
{
    // Apply the player + window settings once, then re-apply whenever they change.
    const auto apply = [this]
    {
        player_.ApplySettings(settings_);
        gfx_.SetVSync(settings_.Get<PlaybackSettings>().videoSync);
    };
    apply();
    framelift::RegisterSettingsChangeCallback(ctx_, apply);

    // Duck audio briefly whenever a UI notification is shown.
    framelift::Subscribe<NotificationEvent>(ctx_, [this](const NotificationEvent&) { player_.PulseDucking(); });

    // Mirror the player idle state so TogglePause can resume the most recent file.
    player_.ObserveProperty(PlayerProperty::IdleActive);
}

void PlaybackControls::Bind()
{
    const KeybindSettings& kb = settings_.Get<KeybindSettings>();

    host::Bind(keys_, "togglePause", kb.togglePause, [this] { TogglePause(); });
    host::Bind(keys_, "toggleFullscreen", kb.toggleFullscreen, [this] { window_.SetFullscreen(!window_.IsFullscreen()); });
    host::Bind(keys_, "quit", kb.quit, [this] { events_.PushQuitEvent(); });
    host::Bind(
        keys_, "toggleNormalize", kb.toggleNormalize,
        [this]
        {
            const bool on = !player_.IsNormalizeEnabled();
            player_.SetAudioNormalize(on, on ? ToAudioNormalizeParams(settings_.Get<AudioSettings>()) : AudioNormalizeParams{});
            ctx_.Publish<NotificationEvent>({on ? "Normalize: On" : "Normalize: Off"});
        }
    );
    host::Bind(keys_, "volumeUp", kb.volumeUp, [this] { AdjustVolumeAndNotify(5); });
    host::Bind(keys_, "volumeDown", kb.volumeDown, [this] { AdjustVolumeAndNotify(-5); });
    host::Bind(
        keys_, "toggleMute", kb.toggleMute,
        [this]
        {
            player_.ToggleMute();
            ctx_.Publish<NotificationEvent>({player_.IsMuted() ? "Mute: On" : "Mute: Off"});
        }
    );
    host::Bind(
        keys_, "toggleSubtitles", kb.toggleSubtitles,
        [this]
        {
            player_.ToggleSubtitles();
            ctx_.Publish<NotificationEvent>({player_.IsSubtitlesEnabled() ? "Subtitles: On" : "Subtitles: Off"});
        }
    );
    host::Bind(keys_, "seekForward", kb.seekForward, [this] { player_.Seek(5); ctx_.Publish<NotificationEvent>({"Seek +Short"}); });
    host::Bind(keys_, "seekBack", kb.seekBack, [this] { player_.Seek(-5); ctx_.Publish<NotificationEvent>({"Seek -Short"}); });
    host::Bind(keys_, "seekForwardLong", kb.seekForwardLong, [this] { player_.Seek(60); ctx_.Publish<NotificationEvent>({"Seek +Long"}); });
    host::Bind(keys_, "seekBackLong", kb.seekBackLong, [this] { player_.Seek(-60); ctx_.Publish<NotificationEvent>({"Seek -Long"}); });
    host::Bind(keys_, "openFileDialog", kb.openFileDialog, [this] { OpenFileDialog(); });
}

void PlaybackControls::TogglePause() const
{
    if (!playerIdle_)
    {
        player_.TogglePause();
        return;
    }

    // Idle: resume the most recent file instead of toggling pause.
    char lastBuf[2048] = {};
    if (const auto* history = ctx_.GetService<IHistory>())
    {
        (void)history->GetMostRecent(lastBuf, sizeof(lastBuf));
    }
    if (!lastBuf[0])
    {
        ctx_.Publish<NotificationEvent>({"No recent files"});
        return;
    }
    if (!std::filesystem::exists(lastBuf))
    {
        ctx_.Publish<NotificationEvent>({"Error: File not found"});
        return;
    }
    ctx_.Publish<OpenFileRequestEvent>({lastBuf, true});
}

void PlaybackControls::AdjustVolumeAndNotify(const int delta) const
{
    player_.AdjustVolume(delta);

    player_.GetDoubleAsync(
        PlayerProperty::Volume,
        [](const double v, const bool ok, void* ud)
        {
            const auto* c = static_cast<VolCell*>(ud);
            IModuleContext* ctx = c->ctx;
            delete c;
            if (ok)
            {
                const std::string msg = "Volume: " + std::to_string(static_cast<int>(v));
                ctx->Publish<NotificationEvent>({msg.c_str()});
            }
        },
        new VolCell{&ctx_}
    );
}

void PlaybackControls::OpenFileDialog() const
{
    fileDialog_.OpenFile(
        [](const char* path, const bool ok, void* ud)
        {
            if (ok && path && path[0])
            {
                static_cast<IModuleContext*>(ud)->Publish<OpenFileRequestEvent>({path, true});
            }
        },
        &ctx_
    );
}
