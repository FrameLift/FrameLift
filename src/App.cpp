#include "App.h"
#include "Cli.h"
#include "IconData.h"
#include "SettingsMapping.h"
#include "platform/gfx/GraphicsApi.h"
#include "platform/gfx/IGraphicsBackend.h"
#include "platform/watch/DirWatcher.h"
#include "platform/ffmpeg/FFmpegPlayer.h"
#include "platform/window/SdlAppWindow.h"
#include "ui/Theme.h"
#include <framelift/ContextHelpers.h>
#include <framelift/Log.h>
#include <framelift/Events.h>
#include <framelift/services/IHistory.h>
#include <framelift/ui/ContextMenuHelpers.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <algorithm>
#include <filesystem>
#include <ranges>

// ── Static storage ────────────────────────────────────────────────────────────

Services App::services_;
PluginRegistry App::registry_;

::Services& App::Services()
{
    return services_;
}

PluginRegistry& App::Registry()
{
    return registry_;
}

// ── Helpers ───────────────────────────────────────────────────────────────────
// ParamsFromSettings / PlaybackOptsFromSettings live in SettingsMapping.h (pure,
// unit-tested).

// ── Constructor / Destructor ──────────────────────────────────────────────────

App::App(const char* title, const int width, const int height, const int cliArgc, const char* const* cliArgv)
    : cliArgc_(cliArgc), cliArgv_(cliArgv), player_(std::make_unique<FFmpegPlayer>()),
      dirWatcher_(CreateDirWatcher())
{
    // ── Phase 1: Platform init ────────────────────────────────────────────────
    // Resolve the pref dir and load settings BEFORE creating the window: the graphics
    // backend — and thus the SDL window flag (SDL_WINDOW_OPENGL vs SDL_WINDOW_VULKAN) —
    // is fixed at window-creation time, so graphics.backend must be known up front.
    char prefBuf[512] = {};
    (void)SdlAppWindow::ResolvePrefPath("", "FrameLift", prefBuf, sizeof(prefBuf));
    const std::string prefDir = prefBuf;
    const std::string settingsPath = prefDir.empty() ? "settings.ini" : prefDir + "settings.ini";

    // App owns the Settings instance; load it before any plugin sees it.
    settings_.Load(settingsPath);

    appWindow_ = std::make_unique<SdlAppWindow>(title, width, height, GraphicsApiFromString(settings_.backend));

    // Let the UI context create plugin-icon textures through the active backend.
    uiCtx_.SetGraphicsBackend(static_cast<IGraphicsBackend*>(appWindow_->GetGraphicsBackend()));

    if (!prefDir.empty())
    {
        const std::string iniPath = prefDir + "imgui.ini";
        appWindow_->SetImGuiIniPath(iniPath.c_str());
    }
    (void)appWindow_->SetWindowIconFromMemory(kIconData, kIconDataSize);

    InitRender();
    InitImGui();

    // Apply the persisted theme before the first frame (GL context is current,
    // NewFrame has not run yet). Seed the snapshot so later Saves can diff.
    Theme::ApplyStyle(settings_);
    Theme::RebuildFonts(settings_);
    appliedTheme_ = {settings_.preset, settings_.accentColor, settings_.fontFile, settings_.fontSize};

    fileDialogService_.Init(appWindow_.get());

    // ── Phase 2: Build PluginContext and register services ────────────────────
    pluginCtx_ = std::make_unique<PluginContext>(prefDir, &settings_, settingsPath);

    pluginCtx_->RegisterService<IMediaPlayer>(player_.get());
    pluginCtx_->RegisterService<IAppWindow>(appWindow_.get());
    pluginCtx_->RegisterService<IDirWatcher>(dirWatcher_.get());
    pluginCtx_->RegisterService<Hotkeys>(&keys_);
    pluginCtx_->RegisterService<FocusManager>(&focus_);
    pluginCtx_->RegisterService<ContextMenu>(&contextMenu_);
    pluginCtx_->RegisterService<IFileDialog>(&fileDialogService_);

    // Playback options update when settings change.
    player_->SetPlaybackOptions(PlaybackOptsFromSettings(settings_));
    player_->SetReadAheadCache(ReadAheadOptsFromSettings(settings_));
    appWindow_->SetVSync(settings_.videoSync);

    // Track idle state for TogglePauseAction (see DrainMediaEvents).
    player_->ObserveProperty(PlayerProperty::IdleActive);
    framelift::RegisterSettingsChangeCallback(
        *pluginCtx_,
        [this]()
        {
            const Settings& s = pluginCtx_->GetSettingsDirect();
            player_->SetPlaybackOptions(PlaybackOptsFromSettings(s));
            player_->SetReadAheadCache(ReadAheadOptsFromSettings(s));
            appWindow_->SetVSync(s.videoSync);
            if (player_->IsNormalizeEnabled())
            {
                player_->SetAudioNormalize(true, ParamsFromSettings(s));
            }

            // This callback fires mid-frame (during SettingsMenu's
            // Save inside Render), so only flag what changed; the
            // actual ImGui work happens at the top of next Render().
            if (s.preset != appliedTheme_.preset || s.accentColor != appliedTheme_.accentColor)
            {
                themeStyleDirty_ = true;
            }
            if (s.fontFile != appliedTheme_.fontFile || s.fontSize != appliedTheme_.fontSize)
            {
                fontAtlasDirty_ = true;
            }
            appliedTheme_ = {s.preset, s.accentColor, s.fontFile, s.fontSize};
        }
    );

    // ── Phase 3: Plugins, then context menu ───────────────────────────────────
    // Plugins register their menu sections during LoadPlugins(); BuildContextMenu()
    // then assembles the menu, replaying those sections between the core items and
    // Quit, so the final order is host core items → plugin items → Quit.
    LoadPlugins();
    BuildContextMenu();

    BuildRenderables();
}

App::~App()
{
    appWindow_->ImGuiShutdown();

    // Clear all DLL-owned lambdas before pluginLoader_ calls FreeLibrary.
    // pluginLoader_ destructs after this body (declared after pluginCtx_),
    // so plugins can still call ctx_->GetService() in their destructors.
    if (pluginCtx_)
    {
        pluginCtx_->ClearSubscriptions();
    }
    contextMenu_.Clear();
    keys_.Clear();

    player_.reset(); // destroy the player's render context before the GL context is torn down
}

// ── Plugin loading ────────────────────────────────────────────────────────────

void App::LoadPlugins()
{
    // Load all enabled plugins from plugins/ subdirectory.
    // Each plugin registers its own context menu section during Install().
    char baseBuf[512] = {};
    (void)appWindow_->GetBasePath(baseBuf, sizeof(baseBuf));
    const std::string pluginsDir = std::string(baseBuf) + "Plugins/";
    pluginLoader_.LoadAll(pluginsDir, settings_.enabledPlugins);

    for (auto& p : pluginLoader_.Plugins())
    {
        // Record identity before Install so a plugin may list peers during it.
        pluginCtx_->AddPlugin(p.name, /*enabled=*/true, p.info);
        Registry().Add(p.plugin, *pluginCtx_);
    }

    // Append any plugin DLLs that are present but currently disabled, so the
    // settings UI can list and re-enable them. Loaded plugins are skipped.
    for (auto& stem : PluginLoader::DiscoverAvailable(pluginsDir))
    {
        const bool loaded = std::ranges::any_of(
            pluginLoader_.Plugins(),
            [&](const auto& p)
            {
                return p.name == stem;
            }
        );
        if (loaded)
        {
            continue;
        }
        const bool enabled = std::ranges::find(settings_.enabledPlugins, stem) != settings_.enabledPlugins.end();
        pluginCtx_->AddPlugin(std::move(stem), enabled, nullptr);
    }

    Registry().BindHotkeys(keys_);
    BindPlayerHotkeys();

    // Materialize plugin settings + keybinds on first run: plugins populate their
    // PluginSettingsImpl caches during Install(), but those reach disk only via
    // SaveSettings(). The startup Settings::Save (App ctor, before plugins) writes
    // host sections only, so without this flush plugin sections appear only after
    // the user presses Save in the Settings menu.
    pluginCtx_->SaveSettings();
}

// ── Context menu ──────────────────────────────────────────────────────────────

void App::BuildContextMenu()
{
    // Host core items come first, then plugin sections (registered during
    // LoadPlugins() and replayed by EmitSections()), then Quit, always last.
    contextMenu_.SetKeys(&keys_);
    framelift::AddItem(
        contextMenu_, "Open File", "openFileDialog",
        [this]
        {
            OpenFileAction();
        }
    );
    framelift::AddItem(
        contextMenu_, "Open Network Stream\xe2\x80\xa6",
        [this]
        {
            // The RemoteStream plugin owns the URL-entry modal and all stream
            // handling; the host just surfaces the entry point next to "Open File".
            pluginCtx_->Publish<OpenNetworkStreamRequestEvent>({});
        }
    );
    contextMenu_.AddSeparator();
    framelift::AddItem(
        contextMenu_, "Play / Pause", "togglePause",
        [this]
        {
            TogglePauseAction();
        }
    );
    framelift::AddItem(
        contextMenu_, "Toggle Fullscreen", "toggleFullscreen",
        [this]
        {
            appWindow_->SetFullscreen(!appWindow_->IsFullscreen());
        }
    );
    contextMenu_.AddSeparator();
    framelift::AddDynamicSubMenu(
        contextMenu_, "Audio",
        [this](UIContext& ctx)
        {
            if (ctx.MenuItem("Toggle Mute", "toggleMute", player_->IsMuted()))
            {
                player_->ToggleMute();
                pluginCtx_->Publish<NotificationEvent>({player_->IsMuted() ? "Mute: On" : "Mute: Off"});
            }
            if (ctx.MenuItem("Normalize", "toggleNormalize", player_->IsNormalizeEnabled()))
            {
                const bool on = !player_->IsNormalizeEnabled();
                player_->SetAudioNormalize(on, on ? ParamsFromSettings(settings_) : AudioNormalizeParams{});
                pluginCtx_->Publish<NotificationEvent>({on ? "Normalize: On" : "Normalize: Off"});
            }
            ctx.Separator();

            struct AudioCtx
            {
                UIContext* ctx;
                IMediaPlayer* player;
                bool empty = true;
            };
            AudioCtx ac{&ctx, player_.get()};
            player_->EnumerateAudioTracks(
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
        contextMenu_, "Subtitles",
        [this](UIContext& ctx)
        {
            if (ctx.MenuItem("Toggle", "toggleSubtitles", player_->IsSubtitlesEnabled()))
            {
                player_->ToggleSubtitles();
                pluginCtx_->Publish<NotificationEvent>(
                    {player_->IsSubtitlesEnabled() ? "Subtitles: On" : "Subtitles: Off"}
                );
            }
            ctx.Separator();

            struct SubCtx
            {
                UIContext* ctx;
                IMediaPlayer* player;
                bool empty = true;
            };
            SubCtx sc{&ctx, player_.get()};
            player_->EnumerateSubtitleTracks(
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

    // Plugin items land here, between the core items and Quit.
    contextMenu_.EmitSections();

    contextMenu_.AddSeparator();
    framelift::AddItem(
        contextMenu_, "Quit", "quit",
        [this]
        {
            appWindow_->PushQuitEvent();
        }
    );
}

// ── Renderables ───────────────────────────────────────────────────────────────

void App::BuildRenderables()
{
    // Collect (order, renderable) pairs from plugins, sort, then insert contextMenu_ at 30.
    using OrderedR = std::pair<int, IRenderable*>;
    std::vector<OrderedR> items;

    for (const auto& p : pluginLoader_.Plugins())
    {
        if (p.renderable)
        {
            items.emplace_back(p.renderOrder, p.renderable);
        }
    }
    items.emplace_back(30, &contextMenu_);

    std::ranges::stable_sort(
        items,
        [](const OrderedR& a, const OrderedR& b)
        {
            return a.first < b.first;
        }
    );

    renderables_.clear();
    for (auto& r : items | std::views::values)
    {
        renderables_.push_back(r);
    }
}

// ── Render setup ──────────────────────────────────────────────────────────────

void App::InitRender() const
{
    // Hand the player the active graphics backend (opaque IGraphicsBackend*); it
    // builds its matching video renderer from it (GL or Vulkan).
    player_->InitRender(appWindow_->GetGraphicsBackend());

    player_->SetRenderUpdateCallback(
        [](void* ud)
        {
            static_cast<App*>(ud)->appWindow_->PushRenderUpdate();
        },
        const_cast<App*>(this)
    );
    player_->SetWakeupCallback(
        [](void* ud)
        {
            static_cast<App*>(ud)->appWindow_->PushPlayerWakeup();
        },
        const_cast<App*>(this)
    );
}

void App::InitImGui() const
{
    appWindow_->ImGuiInit();
}

// ── Frame rendering ───────────────────────────────────────────────────────────

void App::Render()
{
    // Apply any pending theme changes here — before UIBeginFrame, i.e. outside
    // any NewFrame()..Render() pair, which both calls require.
    if (themeStyleDirty_)
    {
        themeStyleDirty_ = false;
        Theme::ApplyStyle(settings_);
    }
    if (fontAtlasDirty_)
    {
        fontAtlasDirty_ = false;
        Theme::RebuildFonts(settings_);
    }

    int w = 0, h = 0;
    appWindow_->GetSize(w, h);

    // Acquire the frame's render target. The backend may decline (e.g. the Vulkan
    // swapchain is being recreated) — skip rendering and presenting this iteration.
    if (!appWindow_->BeginFrame())
    {
        return;
    }

    player_->RenderFrame(w, h);

    appWindow_->UIBeginFrame();
    uiCtx_.UpdateKeys(&keys_);
    uiCtx_.BeginFrame();
    for (auto* r : renderables_)
    {
        r->Render(uiCtx_);
    }
    appWindow_->UIEndFrame();

    appWindow_->SwapBuffers();
}

// ── Event loop helpers ────────────────────────────────────────────────────────

void App::DrainMediaEvents()
{
    while (true)
    {
        const MediaEvent ev = player_->PollEvent();
        if (ev.type == MediaEventType::None)
        {
            break;
        }
        if (ev.type == MediaEventType::PropertyChange && ev.property.prop == PlayerProperty::IdleActive &&
            ev.property.type == PropertyType::Flag)
        {
            playerIdle_ = ev.property.value.flag != 0;
        }
        Registry().OnMediaEvent(ev);
        if (ev.type == MediaEventType::VideoReconfig)
        {
            pendingResize_ = true;
        }
    }
}

void App::Dispatch(const AppEvent& e)
{
    appWindow_->ImGuiProcessEvent(e);
    switch (e.type) // NOLINT(clang-diagnostic-switch-enum)
    {
    case AppEventType::Quit:
        // Let plugins handle their own Quit cleanup (e.g. Playlist::FlushCurrentPos).
        Registry().OnEvent(e);
        Registry().OnShutdown();
        running_ = false;
        return;
    case AppEventType::WindowExposed:
    case AppEventType::RenderUpdate:
        // The loop renders every frame while visible, so these only need to wake
        // WaitNextEvent; the new video frame is consumed by player_->RenderFrame().
        return;
    case AppEventType::PlayerWakeup:
        DrainMediaEvents();
        return;
    default:
        break;
    }

    if (e.type == AppEventType::DropFile)
    {
        const AppEvent::FilePayload& fp = e.AsFile();
        if (fp.filePath && fp.filePath[0])
        {
            pluginCtx_->Publish<OpenFileRequestEvent>({fp.filePath, true});
        }
        return;
    }

    if (fileDialogService_.HandleEvent(e))
    {
        return;
    }

    if (e.type == AppEventType::KeyDown || e.type == AppEventType::KeyUp)
    {
        if (auto* f = focus_.Focused())
        {
            if (f->OnEvent(e))
            {
                return;
            }
        }
        // ReSharper disable once CppExpressionWithoutSideEffects
        keys_.Handle(e);
        return;
    }

    Registry().OnEvent(e);
}

void App::DrainEvents(const int timeoutMs)
{
    AppEvent e;
    (void)appWindow_->WaitNextEvent(e, timeoutMs);
    do
    {
        Dispatch(e);
    } while (appWindow_->PollNextEvent(e));
}

void App::RenderFrame()
{
    Render();
    if (pendingResize_)
    {
        pendingResize_ = false;
        ResizeToVideo();
    }
}

void App::ResizeToVideo() const
{
    if (appWindow_->IsFullscreen())
    {
        return;
    }

    struct SizeState
    {
        const App* app;
    };

    player_->GetDisplaySizeAsync(
        [](const DisplaySize* size, bool ok, void* ud)
        {
            auto* s = static_cast<SizeState*>(ud);
            const App* self = s->app;
            delete s;
            if (!ok || !size || size->width <= 0 || size->height <= 0 || self->appWindow_->IsFullscreen())
            {
                return;
            }

            const Rect usable = self->appWindow_->GetDisplayUsableBounds();
            int w = static_cast<int>(size->width);
            int h = static_cast<int>(size->height);
            const float ratio = self->settings_.maxDisplayRatio;
            const int maxW = static_cast<int>(static_cast<float>(usable.w) * ratio);
            const int maxH = static_cast<int>(static_cast<float>(usable.h) * ratio);

            if (w > maxW || h > maxH)
            {
                const float scale = std::min(
                    static_cast<float>(maxW) / static_cast<float>(w), static_cast<float>(maxH) / static_cast<float>(h)
                );
                w = static_cast<int>(static_cast<float>(w) * scale);
                h = static_cast<int>(static_cast<float>(h) * scale);
            }
            self->appWindow_->SetSize(w, h);
        },
        new SizeState{this}
    );
}

// ── Player actions ────────────────────────────────────────────────────────────

void App::TogglePauseAction() const
{
    if (!playerIdle_)
    {
        player_->TogglePause();
        return;
    }

    // Idle: resume the most recent file instead of toggling pause.
    char lastBuf[2048] = {};
    if (const auto* history = pluginCtx_->GetService<IHistory>())
    {
        (void)history->GetMostRecent(lastBuf, sizeof(lastBuf));
    }
    if (!lastBuf[0])
    {
        pluginCtx_->Publish<NotificationEvent>({"No recent files"});
        return;
    }
    if (!std::filesystem::exists(lastBuf))
    {
        pluginCtx_->Publish<NotificationEvent>({"Error: File not found"});
        return;
    }
    pluginCtx_->Publish<OpenFileRequestEvent>({lastBuf, true});
}

void App::AdjustVolumeAndNotify(const int delta) const
{
    player_->AdjustVolume(delta);

    struct VolCtx
    {
        const App* app;
    };

    player_->GetDoubleAsync(
        PlayerProperty::Volume,
        [](const double v, const bool ok, void* ud)
        {
            const auto* c = static_cast<VolCtx*>(ud);
            const auto* self = c->app;
            delete c;
            if (ok)
            {
                const std::string msg = "Volume: " + std::to_string(static_cast<int>(v));
                self->pluginCtx_->Publish<NotificationEvent>({msg.c_str()});
            }
        },
        new VolCtx{this}
    );
}

void App::OpenFileAction()
{
    fileDialogService_.OpenFile(
        [](const char* path, const bool ok, void* ud)
        {
            if (ok && path && path[0])
            {
                static_cast<App*>(ud)->pluginCtx_->Publish<OpenFileRequestEvent>({path, true});
            }
        },
        this
    );
}

void App::BindPlayerHotkeys()
{
    const Settings& cfg = settings_;

    host::Bind(
        keys_, "togglePause", cfg.togglePause,
        [this]
        {
            TogglePauseAction();
        }
    );
    host::Bind(
        keys_, "toggleFullscreen", cfg.toggleFullscreen,
        [this]
        {
            appWindow_->SetFullscreen(!appWindow_->IsFullscreen());
        }
    );
    host::Bind(
        keys_, "quit", cfg.quit,
        [this]
        {
            appWindow_->PushQuitEvent();
        }
    );
    host::Bind(
        keys_, "toggleNormalize", cfg.toggleNormalize,
        [this]
        {
            const bool on = !player_->IsNormalizeEnabled();
            player_->SetAudioNormalize(on, on ? ParamsFromSettings(settings_) : AudioNormalizeParams{});
            pluginCtx_->Publish<NotificationEvent>({on ? "Normalize: On" : "Normalize: Off"});
        }
    );
    host::Bind(
        keys_, "volumeUp", cfg.volumeUp,
        [this]
        {
            AdjustVolumeAndNotify(5);
        }
    );
    host::Bind(
        keys_, "volumeDown", cfg.volumeDown,
        [this]
        {
            AdjustVolumeAndNotify(-5);
        }
    );
    host::Bind(
        keys_, "toggleMute", cfg.toggleMute,
        [this]
        {
            player_->ToggleMute();
            pluginCtx_->Publish<NotificationEvent>({player_->IsMuted() ? "Mute: On" : "Mute: Off"});
        }
    );
    host::Bind(
        keys_, "toggleSubtitles", cfg.toggleSubtitles,
        [this]
        {
            player_->ToggleSubtitles();
            pluginCtx_->Publish<NotificationEvent>(
                {player_->IsSubtitlesEnabled() ? "Subtitles: On" : "Subtitles: Off"}
            );
        }
    );
    host::Bind(
        keys_, "seekForward", cfg.seekForward,
        [this]
        {
            player_->Seek(5);
            pluginCtx_->Publish<NotificationEvent>({"Seek +Short"});
        }
    );
    host::Bind(
        keys_, "seekBack", cfg.seekBack,
        [this]
        {
            player_->Seek(-5);
            pluginCtx_->Publish<NotificationEvent>({"Seek -Short"});
        }
    );
    host::Bind(
        keys_, "seekForwardLong", cfg.seekForwardLong,
        [this]
        {
            player_->Seek(60);
            pluginCtx_->Publish<NotificationEvent>({"Seek +Long"});
        }
    );
    host::Bind(
        keys_, "seekBackLong", cfg.seekBackLong,
        [this]
        {
            player_->Seek(-60);
            pluginCtx_->Publish<NotificationEvent>({"Seek -Long"});
        }
    );
    host::Bind(
        keys_, "openFileDialog", cfg.openFileDialog,
        [this]
        {
            OpenFileAction();
        }
    );
}

// ── Event loop ────────────────────────────────────────────────────────────────

int App::Run()
{
    // Hand the command line to plugins (all loaded and subscribed by now), then
    // open the first positional file/URL argument — the same request drag-drop and
    // the Open-File dialog publish, so Playlist/RemoteStream route it by scheme.
    pluginCtx_->Publish<CliCommandEvent>({cliArgc_, cliArgv_});
    if (const std::string target = ParseOpenTarget(cliArgc_, cliArgv_); !target.empty())
    {
        pluginCtx_->Publish<OpenFileRequestEvent>({target.c_str(), true});
    }

    // Paint one frame before the loop so the window (created hidden) is shown
    // already displaying the idle screen instead of a black framebuffer. The
    // hidden window emits no WindowExposed event, so we cannot rely on the loop
    // to trigger this first render.
    RenderFrame();

    while (running_)
    {
        const bool renderable = appWindow_->IsRenderable();
        // While visible we render every frame; vsync paces via SwapBuffers (poll
        // events with 0 timeout), vsync-off caps at ~250 fps so a static screen
        // doesn't spin. While minimized/occluded we don't paint, so block on
        // events (100 ms) to idle instead of busy-looping.
        const int timeoutMs = !renderable ? 100 : (settings_.videoSync ? 0 : 4);
        DrainEvents(timeoutMs);
        if (running_ && appWindow_->IsRenderable())
        {
            RenderFrame();
        }
    }
    return 0;
}