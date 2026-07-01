#include "ContextMenuModule.h"

#include <framelift/Hotkeys.h>
#include <framelift/services.h>

#include <QtCore/QTimer>
#include <QtCore/QVariantMap>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <utility>
#include <vector>

// ── ContextMenu service ABI (storage) ───────────────────────────────────────────

void ContextMenuModule::AddItemRaw(const char* label, void (*action)(void*), void* ud, void (*cleanup)(void*)) noexcept
{
    Item item{label ? label : "", {}, action, ud, cleanup};
    item.core = buildingCore_;
    items_.push_back(std::move(item));
    Q_EMIT menuChanged();
}

void ContextMenuModule::AddItemWithHotkeyRaw(
    const char* label, const char* hotkey, void (*action)(void*), void* ud, void (*cleanup)(void*)
) noexcept
{
    Item item{label ? label : "", hotkey ? hotkey : "", action, ud, cleanup};
    item.core = buildingCore_;
    items_.push_back(std::move(item));
    Q_EMIT menuChanged();
}

void ContextMenuModule::AddSeparator() noexcept
{
    items_.push_back({}); // empty label = separator
    Q_EMIT menuChanged();
}

void ContextMenuModule::AddSectionRaw(void (*builder)(ContextMenu&, void*), void* ud, void (*cleanup)(void*)) noexcept
{
    sections_.push_back({builder, ud, cleanup});
}

void ContextMenuModule::EmitSections()
{
    for (auto& section : sections_)
    {
        if (section.builder)
        {
            section.builder(*this, section.ud);
        }
    }
}

void ContextMenuModule::Clear() noexcept
{
    for (auto& item : items_)
    {
        if (item.cleanup)
        {
            item.cleanup(item.ud);
        }
    }
    items_.clear();

    for (auto& section : sections_)
    {
        if (section.cleanup)
        {
            section.cleanup(section.ud);
        }
    }
    sections_.clear();
}

// ── Lifecycle ───────────────────────────────────────────────────────────────────

void ContextMenuModule::OnInstall(IModuleContext& ctx)
{
    playback_ = ctx.GetService<IMediaPlayback>();
    props_ = ctx.GetService<IMediaProperties>();
    audio_ = ctx.GetService<IAudioControl>();
    subtitles_ = ctx.GetService<ISubtitleControl>();
    appWindow_ = ctx.GetService<IAppWindow>();
    events_ = ctx.GetService<IEventPump>();
    fileDialog_ = ctx.GetService<IFileDialog>();
    SetKeys(ctx.GetService<Hotkeys>());

    if (props_)
    {
        props_->ObserveProperty(PlayerProperty::IdleActive);
        // Observe audio state so the menu's checkmarks stay in sync when mute /
        // normalize are toggled elsewhere (e.g. via their hotkeys), not just from
        // this menu's own actions.
        props_->ObserveProperty(PlayerProperty::Mute);
        props_->ObserveProperty(PlayerProperty::Normalize);
    }

    // Register the menu so peer plugins can extend it via AddSection() during their
    // own OnInstall(). This plugin is listed first in the enabled set, so the
    // service is live before any consumer installs.
    ctx.RegisterService<ContextMenu>(this);

    // Invalidate the QmlExtraItems cache on every change to the menu projection.
    QObject::connect(
        this, &ContextMenuModule::menuChanged, this,
        [this]
        {
            extraItemsCacheDirty_ = true;
        }
    );

    // Assemble the menu once, deferred to the first event-loop turn. Peer plugins
    // register their sections during their own OnInstall(), which runs after this
    // one; all OnInstall() calls complete synchronously before App::Run() starts
    // the Qt event loop, so a queued single-shot fires after every section is
    // registered — letting us build the full menu up front instead of lazily
    // inside the QmlExtraItems() getter (which needed a QSignalBlocker to avoid a
    // self-referential binding-loop warning).
    QTimer::singleShot(
        0, this,
        [this]
        {
            if (assembled_)
            {
                return;
            }
            Assemble();
            assembled_ = true;
            Q_EMIT menuChanged(); // marks the cache dirty and refreshes the QML binding
        }
    );
}

void ContextMenuModule::HandleMediaEvent(const MediaEvent& e)
{
    if (e.type == MediaEventType::PropertyChange && e.property.prop == PlayerProperty::IdleActive &&
        e.property.type == PropertyType::Flag)
    {
        playerIdle_ = e.property.value.flag != 0;
        Q_EMIT menuChanged();
        Q_EMIT tracksChanged(); // idle ⇒ no tracks; loaded ⇒ refresh
        return;
    }

    // Mute / normalize can be toggled from their hotkeys; refresh the menu's
    // checkmarks (bound to muted / normalizeEnabled via menuChanged) when the
    // player reports the new state.
    if (e.type == MediaEventType::PropertyChange &&
        (e.property.prop == PlayerProperty::Mute || e.property.prop == PlayerProperty::Normalize) &&
        e.property.type == PropertyType::Flag)
    {
        Q_EMIT menuChanged();
        return;
    }

    // The track lists become available once a file is loaded and change when the
    // audio output is reconfigured; refresh the submenu projections on both.
    if (e.type == MediaEventType::FileLoaded || e.type == MediaEventType::AudioReconfig ||
        e.type == MediaEventType::EndFile)
    {
        Q_EMIT tracksChanged();
    }
}

QVariantList ContextMenuModule::QmlExtraItems()
{
    // The menu is assembled once via a deferred single-shot in OnInstall(); this
    // getter is a pure cached projection, rebuilt only when menuChanged fires.
    if (!extraItemsCacheDirty_)
    {
        return extraItemsCache_;
    }
    QVariantList result;
    for (int i = 0; i < static_cast<int>(items_.size()); ++i)
    {
        const Item& item = items_[i];
        if (!item.action || item.core)
        {
            continue; // separators and host core actions are rendered by QML, not here
        }
        QVariantMap row;
        row.insert(QStringLiteral("label"), QString::fromStdString(item.label));
        row.insert(QStringLiteral("hotkey"), QString::fromStdString(ShortcutFor(item.hotkeyName)));
        row.insert(QStringLiteral("index"), i);
        result.push_back(row);
    }
    extraItemsCache_ = std::move(result);
    extraItemsCacheDirty_ = false;
    return extraItemsCache_;
}

QVariantList ContextMenuModule::QmlAudioTracks() const
{
    QVariantList rows;
    if (audio_)
    {
        audio_->EnumerateAudioTracks(
            [](const AudioTrack* t, void* ud)
            {
                auto* out = static_cast<QVariantList*>(ud);
                QVariantMap row;
                row.insert(QStringLiteral("id"), static_cast<qlonglong>(t->id));
                row.insert(QStringLiteral("label"), QString::fromUtf8(t->label));
                row.insert(QStringLiteral("selected"), t->selected);
                out->push_back(row);
            },
            &rows
        );
    }
    return rows;
}

QVariantList ContextMenuModule::QmlSubtitleTracks() const
{
    QVariantList rows;
    if (subtitles_)
    {
        subtitles_->EnumerateSubtitleTracks(
            [](const SubtitleTrack* t, void* ud)
            {
                auto* out = static_cast<QVariantList*>(ud);
                QVariantMap row;
                row.insert(QStringLiteral("id"), static_cast<qlonglong>(t->id));
                row.insert(QStringLiteral("label"), QString::fromUtf8(t->label));
                row.insert(QStringLiteral("selected"), t->selected);
                out->push_back(row);
            },
            &rows
        );
    }
    return rows;
}

QVariantMap ContextMenuModule::QmlCoreShortcuts() const
{
    QVariantMap shortcuts;
    for (const char* name :
         {"openFileDialog", "togglePause", "toggleFullscreen", "toggleMute", "toggleNormalize", "toggleSubtitles",
          "quit"})
    {
        shortcuts.insert(QString::fromUtf8(name), QString::fromStdString(ShortcutFor(name)));
    }
    return shortcuts;
}

void ContextMenuModule::selectAudioTrack(const qint64 id)
{
    if (audio_)
    {
        audio_->SelectAudioTrack(id);
        Q_EMIT tracksChanged();
    }
}

void ContextMenuModule::selectSubtitleTrack(const qint64 id)
{
    if (subtitles_)
    {
        subtitles_->SelectSubtitleTrack(id);
        Q_EMIT tracksChanged();
        Q_EMIT menuChanged(); // selecting a track can flip the subtitles-enabled state
    }
}

void ContextMenuModule::openNetwork()
{
    if (ctx_)
    {
        ctx_->Publish<OpenNetworkStreamRequestEvent>({});
    }
}

void ContextMenuModule::toggleFullscreen()
{
    if (appWindow_)
    {
        appWindow_->SetFullscreen(!appWindow_->IsFullscreen());
    }
}

void ContextMenuModule::toggleMute()
{
    if (audio_)
    {
        audio_->ToggleMute();
        Q_EMIT menuChanged();
    }
}

void ContextMenuModule::toggleNormalize()
{
    if (audio_)
    {
        const bool on = !audio_->IsNormalizeEnabled();
        audio_->SetAudioNormalize(on, on ? NormalizeParams() : AudioNormalizeParams{});
        Q_EMIT menuChanged();
    }
}

void ContextMenuModule::toggleSubtitles()
{
    if (subtitles_)
    {
        subtitles_->ToggleSubtitles();
        Q_EMIT menuChanged();
    }
}

void ContextMenuModule::invokeExtra(const int index)
{
    if (index >= 0 && index < static_cast<int>(items_.size()) && items_[index].action)
    {
        items_[index].action(items_[index].ud);
    }
}

void ContextMenuModule::quit()
{
    if (events_)
    {
        events_->PushQuitEvent();
    }
}

void ContextMenuModule::HandleShutdown()
{
    // Free every DLL-owned closure (this plugin's items + peer-registered sections)
    // while all plugin DLLs are still loaded — Registry::OnShutdown() runs before
    // any FreeLibrary. Mirrors the former host ~App ordering.
    Clear();
}

// ── Menu assembly ───────────────────────────────────────────────────────────────

void ContextMenuModule::Assemble()
{
    buildingCore_ = true;
    BuildCoreItems();
    buildingCore_ = false;

    // Plugin sections land here, between the core items and Quit. They are not
    // core, so they flow through to extraItems for QML to render.
    EmitSections();

    AddSeparator();
    buildingCore_ = true;
    framelift::AddItem(
        *this, "Quit", "quit",
        [this]
        {
            if (events_)
            {
                events_->PushQuitEvent();
            }
        }
    );
    buildingCore_ = false;
}

void ContextMenuModule::BuildCoreItems()
{
    framelift::AddItem(
        *this, "Open File", "openFileDialog",
        [this]
        {
            OpenFileAction();
        }
    );
    framelift::AddItem(
        *this, "Open Network Stream\xe2\x80\xa6",
        [this]
        {
            // The RemoteStream plugin owns the URL-entry modal and all stream
            // handling; the menu just surfaces the entry point next to "Open File".
            if (ctx_)
            {
                ctx_->Publish<OpenNetworkStreamRequestEvent>({});
            }
        }
    );
    AddSeparator();
    framelift::AddItem(
        *this, "Play / Pause", "togglePause",
        [this]
        {
            TogglePauseAction();
        }
    );
    framelift::AddItem(
        *this, "Toggle Fullscreen", "toggleFullscreen",
        [this]
        {
            if (appWindow_)
            {
                appWindow_->SetFullscreen(!appWindow_->IsFullscreen());
            }
        }
    );
}

// ── Actions ─────────────────────────────────────────────────────────────────────

void ContextMenuModule::OpenFileAction()
{
    if (!fileDialog_ || !ctx_)
    {
        return;
    }
    fileDialog_->OpenFile(
        [](const char* path, const bool ok, void* ud)
        {
            if (ok && path && path[0])
            {
                static_cast<IModuleContext*>(ud)->Publish<OpenFileRequestEvent>({path, true});
            }
        },
        ctx_
    );
}

void ContextMenuModule::TogglePauseAction()
{
    if (!playback_)
    {
        return;
    }
    if (!playerIdle_)
    {
        playback_->TogglePause();
        return;
    }

    // Idle: resume the most recent file instead of toggling pause.
    char lastBuf[2048] = {};
    if (ctx_)
    {
        if (const auto* history = ctx_->GetService<IHistory>())
        {
            (void)history->GetMostRecent(lastBuf, sizeof(lastBuf));
        }
    }
    if (!lastBuf[0])
    {
        if (ctx_)
        {
            ctx_->Publish<NotificationEvent>({"No recent files"});
        }
        return;
    }
    if (!std::filesystem::exists(lastBuf))
    {
        if (ctx_)
        {
            ctx_->Publish<NotificationEvent>({"Error: File not found"});
        }
        return;
    }
    if (ctx_)
    {
        ctx_->Publish<OpenFileRequestEvent>({lastBuf, true});
    }
}

AudioNormalizeParams ContextMenuModule::NormalizeParams() const
{
    AudioNormalizeParams p;
    if (auto* s = ctx_ ? ctx_->GetService<ISettingsStore>() : nullptr)
    {
        p.frameLen = s->GetSettingInt("audio.dynaudnormFrameLen");
        p.gaussSize = s->GetSettingInt("audio.dynaudnormGaussSize");
        p.peak = s->GetSettingFloat("audio.dynaudnormPeak");
        p.maxGain = s->GetSettingFloat("audio.dynaudnormMaxGain");
        p.volume = s->GetSettingFloat("audio.dynaudnormVolume");
    }
    return p;
}

std::string ContextMenuModule::ShortcutFor(const std::string& hotkeyName) const
{
    if (!keys_ || hotkeyName.empty())
    {
        return {};
    }
    const int len = keys_->GetShortcutString(hotkeyName.c_str(), nullptr, 0);
    if (len <= 0)
    {
        return {};
    }
    std::string value(static_cast<std::size_t>(len + 1), '\0');
    keys_->GetShortcutString(hotkeyName.c_str(), value.data(), len + 1);
    value.resize(static_cast<std::size_t>(len));
    return value;
}
