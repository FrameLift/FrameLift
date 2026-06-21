#include "ContextMenuModule.h"

#include <framelift/Hotkeys.h>
#include <framelift/services.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <utility>
#include <vector>

// ── ContextMenu service ABI (storage) ───────────────────────────────────────────

void ContextMenuModule::AddItemRaw(const char* label, void (*action)(void*), void* ud, void (*cleanup)(void*)) noexcept
{
    items_.push_back({label ? label : "", {}, action, ud, cleanup});
}

void ContextMenuModule::AddItemWithHotkeyRaw(
    const char* label, const char* hotkey, void (*action)(void*), void* ud, void (*cleanup)(void*)
) noexcept
{
    items_.push_back({label ? label : "", hotkey ? hotkey : "", action, ud, cleanup});
}

void ContextMenuModule::AddSeparator() noexcept
{
    items_.push_back({}); // empty label = separator
}

void ContextMenuModule::AddDynamicSubMenuRaw(
    const char* label, void (*builder)(void*, UIContext&), void* ud, void (*cleanup)(void*)
) noexcept
{
    Item item;
    item.label = label ? label : "";
    item.dynamicBuilder = builder;
    item.builderUd = ud;
    item.cleanup = cleanup;
    items_.push_back(std::move(item));
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

// ── Rendering ───────────────────────────────────────────────────────────────────

void ContextMenuModule::RenderItems(UIContext& ctx, std::vector<Item>& items)
{
    for (auto& item : items)
    {
        if (item.label.empty())
        {
            ctx.Separator();
        }
        else if (item.dynamicBuilder)
        {
            if (ctx.BeginMenu(item.label.c_str()))
            {
                item.dynamicBuilder(item.builderUd, ctx);
                ctx.EndMenu();
            }
        }
        else if (!item.children.empty())
        {
            if (ctx.BeginMenu(item.label.c_str()))
            {
                RenderItems(ctx, item.children);
                ctx.EndMenu();
            }
        }
        else
        {
            const char* sc = nullptr;
            char scBuf[64] = {};
            if (keys_ && !item.hotkeyName.empty())
            {
                if (keys_->GetShortcutString(item.hotkeyName.c_str(), scBuf, sizeof(scBuf)) > 0)
                {
                    sc = scBuf;
                }
            }
            if (ctx.MenuItem(item.label.c_str(), sc) && item.action)
            {
                item.action(item.ud);
            }
        }
    }
}

void ContextMenuModule::OnRender(UIContext& ctx)
{
    // Assemble on the first frame: by now every other plugin has installed and
    // registered its section, so EmitSections() lands them between the core items
    // and Quit, preserving the former host-built order.
    if (!assembled_)
    {
        Assemble();
        assembled_ = true;
    }

    if (ctx.BeginPopupContextVoid("##main_ctx"))
    {
        RenderItems(ctx, items_);
        ctx.EndPopup();
    }
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
    }

    // Register the menu so peer plugins can extend it via AddSection() during their
    // own OnInstall(). This plugin is listed first in the enabled set, so the
    // service is live before any consumer installs.
    ctx.RegisterService<ContextMenu>(this);
}

void ContextMenuModule::HandleMediaEvent(const MediaEvent& e)
{
    if (e.type == MediaEventType::PropertyChange && e.property.prop == PlayerProperty::IdleActive &&
        e.property.type == PropertyType::Flag)
    {
        playerIdle_ = e.property.value.flag != 0;
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
    BuildCoreItems();

    // Plugin sections land here, between the core items and Quit.
    EmitSections();

    AddSeparator();
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
    AddSeparator();
    framelift::AddDynamicSubMenu(
        *this, "Audio",
        [this](UIContext& ctx)
        {
            if (!audio_)
            {
                return;
            }
            if (ctx.MenuItem("Toggle Mute", "toggleMute", audio_->IsMuted()))
            {
                audio_->ToggleMute();
                if (ctx_)
                {
                    ctx_->Publish<NotificationEvent>({audio_->IsMuted() ? "Mute: On" : "Mute: Off"});
                }
            }
            if (ctx.MenuItem("Normalize", "toggleNormalize", audio_->IsNormalizeEnabled()))
            {
                const bool on = !audio_->IsNormalizeEnabled();
                audio_->SetAudioNormalize(on, on ? NormalizeParams() : AudioNormalizeParams{});
                if (ctx_)
                {
                    ctx_->Publish<NotificationEvent>({on ? "Normalize: On" : "Normalize: Off"});
                }
            }
            ctx.Separator();

            if (ctx.BeginMenu("Output device"))
            {
                struct DeviceCtx
                {
                    UIContext* ctx;
                    IAudioControl* player;
                    bool empty = true;
                };
                DeviceCtx dc{&ctx, audio_};
                audio_->EnumerateAudioOutputDevices(
                    [](const AudioOutputDevice* d, void* ud)
                    {
                        auto* state = static_cast<DeviceCtx*>(ud);
                        state->empty = false;
                        const char* label = d->isDefault ? "System default" : d->name;
                        if (state->ctx->MenuItem(label, d->selected))
                        {
                            AudioPreferences prefs = state->player->GetAudioPreferences();
                            std::strncpy(prefs.outputDevice, d->name, sizeof(prefs.outputDevice) - 1);
                            prefs.outputDevice[sizeof(prefs.outputDevice) - 1] = '\0';
                            state->player->SetAudioPreferences(prefs);
                        }
                    },
                    &dc
                );
                if (dc.empty)
                {
                    ctx.TextDisabled("No output devices");
                }
                ctx.EndMenu();
            }

            if (ctx.BeginMenu("Sync offset"))
            {
                const AudioPreferences prefs = audio_->GetAudioPreferences();
                char current[64];
                std::snprintf(current, sizeof(current), "Current: %+d ms", prefs.syncOffsetMs);
                ctx.TextDisabled(current);
                if (ctx.MenuItem("-50 ms"))
                {
                    AudioPreferences p = audio_->GetAudioPreferences();
                    p.syncOffsetMs -= 50;
                    audio_->SetAudioPreferences(p);
                }
                if (ctx.MenuItem("+50 ms"))
                {
                    AudioPreferences p = audio_->GetAudioPreferences();
                    p.syncOffsetMs += 50;
                    audio_->SetAudioPreferences(p);
                }
                if (ctx.MenuItem("Reset"))
                {
                    AudioPreferences p = audio_->GetAudioPreferences();
                    p.syncOffsetMs = 0;
                    audio_->SetAudioPreferences(p);
                }
                ctx.EndMenu();
            }
            ctx.Separator();

            struct AudioCtx
            {
                UIContext* ctx;
                IAudioControl* player;
                bool empty = true;
            };
            AudioCtx ac{&ctx, audio_};
            audio_->EnumerateAudioTracks(
                [](const AudioTrack* t, void* ud)
                {
                    auto* a = static_cast<AudioCtx*>(ud);
                    a->empty = false;
                    if (a->ctx->MenuItem(t->label, t->selected))
                    {
                        a->player->SelectAudioTrack(t->id);
                    }
                },
                &ac
            );
            if (ac.empty)
            {
                ctx.TextDisabled("No audio tracks");
            }
        }
    );
    framelift::AddDynamicSubMenu(
        *this, "Subtitles",
        [this](UIContext& ctx)
        {
            if (!subtitles_)
            {
                return;
            }
            if (ctx.MenuItem("Toggle", "toggleSubtitles", subtitles_->IsSubtitlesEnabled()))
            {
                subtitles_->ToggleSubtitles();
                if (ctx_)
                {
                    ctx_->Publish<NotificationEvent>(
                        {subtitles_->IsSubtitlesEnabled() ? "Subtitles: On" : "Subtitles: Off"}
                    );
                }
            }
            ctx.Separator();

            struct SubCtx
            {
                UIContext* ctx;
                ISubtitleControl* player;
                bool empty = true;
            };
            SubCtx sc{&ctx, subtitles_};
            subtitles_->EnumerateSubtitleTracks(
                [](const SubtitleTrack* t, void* ud)
                {
                    auto* s = static_cast<SubCtx*>(ud);
                    s->empty = false;
                    if (s->ctx->MenuItem(t->label, t->selected))
                    {
                        s->player->SelectSubtitleTrack(t->id);
                    }
                },
                &sc
            );
            if (sc.empty)
            {
                ctx.TextDisabled("No subtitles");
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
