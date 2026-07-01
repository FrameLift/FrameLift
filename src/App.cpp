#include "App.h"
#include "Cli.h"
#include "CoreCommands.h"
#include "CoreSettings.h"
#include "FFmpegPlayer.h"
#include "GraphicsApi.h"
#include "IGraphicsBackend.h"
#include "LogBuffer.h"
#include "QtAppWindow.h"
#if FRAMELIFT_MODULE_WIN_SHELL
#include "WinShell.h"
#endif
#include <framelift/ContextHelpers.h>
#include <framelift/Events.h>
#include <framelift/IModule.h>
#include <framelift/Log.h>
#include <framelift/services/ILogBuffer.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#if FRAMELIFT_BUILD_LAUNCH_TESTS
#include <QtCore/QTimer>
#endif

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <unordered_set>

namespace
{
std::string LegacyPluginId(std::string id)
{
    if (id.ends_with(".core"))
    {
        id.erase(id.size() - 5);
    }
    return id;
}

void MigrateLegacyPluginConfig(const std::string& legacyPath, PluginConfig& pluginConfig)
{
    std::ifstream file(legacyPath);
    if (!file)
    {
        return;
    }

    std::string line;
    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        if (line.empty() || line.front() == '#')
        {
            continue;
        }

        const auto eq = line.find('=');
        if (eq == std::string::npos)
        {
            continue;
        }

        const std::string id = LegacyPluginId(line.substr(0, eq));
        const std::string value = line.substr(eq + 1);
        if (!id.empty())
        {
            pluginConfig.Set(id, value != "disabled");
        }
    }
}

} // namespace

// ── Constructor / Destructor ──────────────────────────────────────────────────

App::App(
    const char* title, const int width, const int height, const GraphicsApi graphicsApi, const int cliArgc,
    const char* const* cliArgv
)
    : cliArgc_(cliArgc), cliArgv_(cliArgv), player_(std::make_unique<FFmpegPlayer>())
{
    FRAMELIFT_PERF_START("app-start");

    ffmpeg_ = player_.get();

    char prefBuf[512] = {};
    (void)QtAppWindow::ResolvePrefPath("", "FrameLift", prefBuf, sizeof(prefBuf));
    const std::string prefDir = prefBuf;
    const std::string settingsPath = prefDir.empty() ? "settings.ini" : prefDir + "settings.ini";
    pluginsPath_ = prefDir.empty() ? "plugins.ini" : prefDir + "plugins.ini";

    InitPlatform(title, width, height, graphicsApi, prefDir, settingsPath);
    InitServices(prefDir, settingsPath);

    // The ContextMenu plugin owns the right-click menu: it registers the ContextMenu
    // service that other plugins extend, and assembles its items on its first frame.
    LoadPlugins();

    BuildPluginViews();
}

App::~App()
{
    // Clear all DLL-owned lambdas before pluginLoader_ calls FreeLibrary.
    // pluginLoader_ destructs after this body (declared after moduleCtx_),
    // so modules can still call ctx_->GetService() in their destructors.
    if (moduleCtx_)
    {
        moduleCtx_->ClearSubscriptions();
    }
    keys_.Clear();

    if (appWindow_)
    {
        appWindow_->SetEventSink({});
        appWindow_->SetPlayerWakeupHandler({});
        appWindow_->SetGraphicsInvalidatedHandler({});
        appWindow_->SetVideoRenderCallbacks({}, {});
    }
    player_.reset();    // destroy the player's render context before the GL context is torn down
    appWindow_.reset(); // destroy QML roots before pluginLoader_ unloads plugin DLLs/view models
}

// ── Construction phases ─────────────────────────────────────────────────────────

void App::InitPlatform(
    const char* title, const int width, const int height, const GraphicsApi graphicsApi, const std::string& prefDir,
    const std::string& settingsPath
)
{
    // App owns the Settings instance; load it before any module sees it.
    settings_.Load(settingsPath);

    // User plugin enablement manifest (opt-out): load before plugins are scanned.
    pluginConfig_.Load(pluginsPath_);
    const std::string legacyPath = std::filesystem::path(pluginsPath_).parent_path().empty()
                                       ? std::string("packages.ini")
                                       : (std::filesystem::path(pluginsPath_).parent_path() / "packages.ini").string();
    MigrateLegacyPluginConfig(legacyPath, pluginConfig_);

    appWindow_ = std::make_unique<QtAppWindow>(title, width, height, graphicsApi);

    (void)appWindow_->SetWindowIcon(":/framelift/assets/icon.svg");

    SetupPlayerCallbacks();

    fileDialogService_.Init(appWindow_.get(), appWindow_.get());
}

void App::InitServices(const std::string& prefDir, const std::string& settingsPath)
{
    moduleCtx_ = std::make_unique<ModuleContext>(prefDir, &settings_, settingsPath, &pluginConfig_, pluginsPath_);

    // The one FFmpegPlayer is registered under each capability interface it implements.
    // Register each separately: the variadic RegisterService can't sibling-cast a
    // concrete pointer across unrelated bases. Plugins fetch only the facets they use.
    moduleCtx_->RegisterService<IMediaPlayback>(player_.get());
    moduleCtx_->RegisterService<IMediaProperties>(player_.get());
    moduleCtx_->RegisterService<IVideoOutput>(player_.get());
    moduleCtx_->RegisterService<IAudioControl>(player_.get());
    moduleCtx_->RegisterService<ISubtitleControl>(player_.get());
    // The one QtAppWindow is registered under each plugin-visible window facet it implements.
    moduleCtx_->RegisterService<IAppWindow>(appWindow_.get());
    moduleCtx_->RegisterService<IEventPump>(appWindow_.get());
    moduleCtx_->RegisterService<Hotkeys>(&keys_);
    moduleCtx_->RegisterService<IFileDialog>(&fileDialogService_);
    moduleCtx_->RegisterService<IJson>(&jsonService_);
    moduleCtx_->RegisterService<ILogBuffer>(&HostLogBuffer());
    graphicsInfo_ = std::make_unique<GraphicsInfoService>(appWindow_.get());
    moduleCtx_->RegisterService<IGraphicsInfo>(graphicsInfo_.get());
    host::RegisterCoreCommands(moduleCtx_->Commands(), *moduleCtx_, *ffmpeg_, *appWindow_, pluginsPath_);

    // Controllers own their own event-bus wiring (settings re-apply, audio ducking,
    // theme reaction) so App holds no subscriptions.
    playbackControls_ = std::make_unique<PlaybackControls>(
        keys_, settings_, *ffmpeg_, *appWindow_, *appWindow_, fileDialogService_, *moduleCtx_
    );
    playbackControls_->Connect();

    // With plugins excluded this phase, no module turns an OpenFileRequestEvent (from the
    // CLI arg, drag-drop, or the file dialog) into an actual load — the Playlist plugin
    // normally does. Subscribe here so the host opens the requested file directly, which is
    // what makes the Phase-1 "video plays" milestone reachable without plugins.
    framelift::Subscribe<OpenFileRequestEvent>(
        *moduleCtx_,
        [this](const OpenFileRequestEvent& e)
        {
            if (e.path && e.path[0])
            {
                ffmpeg_->LoadFile(e.path, 0.0);
            }
        }
    );

    // Playlist publishes this when its last item ends with nothing to advance to.
    // Stop the player so it returns to the idle screen rather than holding the final
    // frame in a seekable "playing" state.
    framelift::Subscribe<StopPlaybackRequestEvent>(
        *moduleCtx_,
        [this](const StopPlaybackRequestEvent&)
        {
            ffmpeg_->Stop();
        }
    );

    // Qt owns the loop: route the window's GUI-thread hooks back into App. Input/events
    // → Dispatch; player worker wakeups → OnPlayerWakeup (drain media events); the
    // scene-graph video node → RenderVideo (the host video draw).
    appWindow_->SetEventSink(
        [this](const AppEvent& e)
        {
            Dispatch(e);
        }
    );
    appWindow_->SetPlayerWakeupHandler(
        [this]
        {
            OnPlayerWakeup();
        }
    );
    appWindow_->SetGraphicsInvalidatedHandler(
        [this]
        {
            player_->ReleaseRender();
            renderInit_ = false;
        }
    );
    appWindow_->SetVideoRenderCallbacks(
        [this](int fbW, int fbH)
        {
            PrepareVideo(fbW, fbH);
        },
        [this](int fbW, int fbH)
        {
            RenderVideo(fbW, fbH);
        }
    );

#if FRAMELIFT_MODULE_WIN_SHELL
    // Windows shell integration consumes the same services/events; wire it after
    // they're registered so its Connect() can resolve them.
    winShell_ = std::make_unique<WinShell>();
    winShell_->Connect(*moduleCtx_);
#endif
}

// ── Plugin loading ──────────────────────────────────────────────────────────────

void App::LoadPlugins()
{
    // Load every plugin present in the plugins/ subdirectory. Plugin enablement
    // comes from plugins.ini (keyed by plugin id); the loader resolves dependencies
    // and load order from embedded metadata, then instantiates each enabled plugin's
    // single module.
    char baseBuf[512] = {};
    (void)appWindow_->GetBasePath(baseBuf, sizeof(baseBuf));
    const std::string pluginsDir = std::string(baseBuf) + "plugins/";
    pluginLoader_.LoadAll(pluginsDir, pluginConfig_.DisabledIds());

    // What actually got instantiated this session, by plugin id.
    std::unordered_set<std::string> loadedPluginIds;
    for (const auto& p : pluginLoader_.Plugins())
    {
        loadedPluginIds.insert(p.pluginId);
    }

    // Build the catalogue from every discovered plugin (loaded or not) so the
    // settings UI can list and toggle each plugin. Populate it before Install() so a
    // plugin module may enumerate peers during it.
    std::vector<std::string> discoveredPluginIds;
    for (auto& plugin : PluginLoader::DiscoverAvailable(pluginsDir))
    {
        PluginCatalog::PluginCatalogEntry entry;
        entry.id = plugin.pluginId;
        discoveredPluginIds.push_back(plugin.pluginId);
        entry.displayName = std::move(plugin.displayName);
        entry.version[0] = plugin.version[0];
        entry.version[1] = plugin.version[1];
        entry.version[2] = plugin.version[2];
        entry.publisher = std::move(plugin.publisher);
        entry.description = std::move(plugin.description);
        entry.enabled = pluginConfig_.IsEnabled(entry.id);
        entry.loaded = loadedPluginIds.contains(entry.id);
        moduleCtx_->Catalog().AddPlugin(std::move(entry));
    }

    // Install each loaded plugin module.
    for (auto& p : pluginLoader_.Plugins())
    {
        registry_.Add(p.module, *moduleCtx_);
    }

    // Refresh the manifest so it lists every discovered plugin (new ones default to
    // enabled), keeping plugins.ini a complete, hand-editable record.
    pluginConfig_.EnsureKnown(discoveredPluginIds);
    pluginConfig_.Save(pluginsPath_);

    registry_.BindHotkeys(keys_);
    playbackControls_->Bind();

    // Materialize module settings + keybinds on first run: modules populate their
    // ModuleSettingsImpl caches during Install(), but those reach disk only via
    // SaveSettings(). The startup Settings::Save (InitPlatform, before plugins)
    // writes host sections only, so without this flush module sections appear only
    // after the user presses Save in the Settings menu.
    moduleCtx_->Settings().SaveSettings();
}

// ── Renderables ───────────────────────────────────────────────────────────────

void App::BuildPluginViews()
{
    std::vector<QmlViewSpec> views;

    for (const auto& p : pluginLoader_.Plugins())
    {
        if (p.viewModel && !p.qmlEntryUrl.empty())
        {
            views.push_back(
                {QString::fromStdString(p.pluginId), QString::fromStdString(p.qmlEntryUrl), p.viewModel, p.renderOrder}
            );
        }
    }
    appWindow_->SetPluginViews(std::move(views));
}

// ── Render setup ──────────────────────────────────────────────────────────────

void App::SetupPlayerCallbacks()
{
    // Worker-thread wakeups → the window's queued signals (no GL here; the renderer is
    // built lazily on the first RenderVideo when Qt's GL context is current).
    player_->SetRenderUpdateCallback(
        [](void* ud)
        {
            static_cast<App*>(ud)->appWindow_->PushRenderUpdate();
        },
        this
    );
    player_->SetWakeupCallback(
        [](void* ud)
        {
            static_cast<App*>(ud)->appWindow_->PushPlayerWakeup();
        },
        this
    );
}

// ── Frame rendering ───────────────────────────────────────────────────────────

void App::PrepareVideo(const int fbW, const int fbH)
{
    // The first scene-graph frame exposes the native graphics objects. Adopt Qt's
    // OpenGL context or Vulkan frame resources, then build the matching video renderer.
    if (!renderInit_)
    {
        auto* backend = static_cast<IGraphicsBackend*>(appWindow_->GetGraphicsBackend());
        if (!backend)
        {
            // A failed backend init must not propagate a null handle into the player's
            // renderer. Leave renderInit_ false so this retries on the next frame.
            Log::Warn("App: graphics backend unavailable; deferring render init");
            return;
        }
        backend->OnQtWindowCreated(static_cast<QQuickWindow*>(appWindow_->GetNativeHandle()));
        player_->InitRender(backend);
        renderInit_ = true;
    }

    if (auto* backend = static_cast<IGraphicsBackend*>(appWindow_->GetGraphicsBackend()))
    {
        backend->PrepareQtFrame(static_cast<QQuickWindow*>(appWindow_->GetNativeHandle()));
    }
    player_->PrepareRenderFrame(fbW, fbH);
}

void App::RenderVideo(const int fbW, const int fbH)
{
    if (auto* backend = static_cast<IGraphicsBackend*>(appWindow_->GetGraphicsBackend()))
    {
        backend->PrepareQtFrame(static_cast<QQuickWindow*>(appWindow_->GetNativeHandle()));
    }
    player_->DrawPreparedFrame(fbW, fbH);
}

void App::OnPlayerWakeup()
{
    DrainMediaEvents();
    if (pendingResize_)
    {
        pendingResize_ = false;
        ResizeToVideo();
    }
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

        // A media state change other than the routine position tick should repaint
        // promptly even while the controls are hidden (EOF → idle screen, buffering
        // spinner, the pause icon, …). Position updates are excluded so steady playback
        // isn't pinned to the display refresh by a redraw it doesn't need. pendingVideoRedraw_
        // both triggers a render (it is in the gate) and refreshes the cached video layer.
        const bool positionTick =
            ev.type == MediaEventType::PropertyChange &&
            (ev.property.prop == PlayerProperty::TimePos || ev.property.prop == PlayerProperty::PercentPos);
        if (!positionTick)
        {
            pendingVideoRedraw_ = true;
        }

        if (ev.type == MediaEventType::PropertyChange && ev.property.prop == PlayerProperty::IdleActive &&
            ev.property.type == PropertyType::Flag)
        {
            playbackControls_->SetPlayerIdle(ev.property.value.flag != 0);
        }

        // A file that ended for any reason other than clean EOF failed — tell the user
        // why. Overlay subscribes to NotificationEvent and shows the toast.
        if (ev.type == MediaEventType::EndFile && ev.endReason != EndFileReason::Eof && moduleCtx_)
        {
            const char* msg = "Couldn't play file";
            switch (ev.endReason)
            {
            case EndFileReason::NotFound:
                msg = "File not found";
                break;
            case EndFileReason::Unsupported:
                msg = "Unsupported format or codec";
                break;
            case EndFileReason::Corrupt:
                msg = "File is corrupt or truncated";
                break;
            case EndFileReason::NoStream:
                msg = "No playable audio or video stream";
                break;
            default:
                break; // Error / Other → generic message above
            }
            moduleCtx_->Publish<NotificationEvent>({msg});
        }

        // Non-fatal notice: a stream was dropped (unsupported decoder) but the file still
        // plays. Tell the user which fallback is in effect.
        if (ev.type == MediaEventType::Notice && moduleCtx_)
        {
            const char* msg = nullptr;
            switch (static_cast<MediaNoticeKind>(ev.property.value.i64))
            {
            case MediaNoticeKind::VideoUnsupported:
                msg = "Unsupported video codec - playing audio only";
                break;
            case MediaNoticeKind::AudioUnsupported:
                msg = "Unsupported audio codec - playing video only";
                break;
            }
            if (msg)
            {
                moduleCtx_->Publish<NotificationEvent>({msg});
            }
        }
#if FRAMELIFT_MODULE_WIN_SHELL
        if (winShell_)
        {
            winShell_->OnMediaEvent(ev);
        }
#endif
        registry_.OnMediaEvent(ev);
        if (ev.type == MediaEventType::VideoReconfig)
        {
            pendingResize_ = true;
        }
    }
}

void App::Dispatch(const AppEvent& e)
{
    // Qt owns the loop and schedules repaints (QQuickWindow::update) after input and on
    // worker wakeups, so Dispatch only routes the event — no paint-gating bookkeeping.
    // RenderUpdate/PlayerWakeup never arrive here: they are delivered as queued signals
    // straight to QtAppWindow (update()) and OnPlayerWakeup() respectively.
    switch (e.type) // NOLINT(clang-diagnostic-switch-enum)
    {
    case AppEventType::Quit:
        // Let modules handle their own Quit cleanup (e.g. Playlist::FlushCurrentPos).
        registry_.OnEvent(e);
        registry_.OnShutdown();
        appWindow_->PushQuitEvent(); // QGuiApplication::quit() on the GUI thread
        return;
    default:
        break;
    }

    if (e.type == AppEventType::DropFile)
    {
        const AppEvent::FilePayload& fp = e.AsFile();
        if (fp.filePath && fp.filePath[0])
        {
            moduleCtx_->Publish<OpenFileRequestEvent>({fp.filePath, true});
        }
        return;
    }

    if (fileDialogService_.HandleEvent(e))
    {
        return;
    }

    if (e.type == AppEventType::KeyDown || e.type == AppEventType::KeyUp)
    {
        if (registry_.OnEvent(e))
        {
            return;
        }
        keys_.Handle(e);
        return;
    }

    registry_.OnEvent(e);
}

#if FRAMELIFT_BUILD_LAUNCH_TESTS
void App::ScheduleTestExitIfRequested()
{
    bool ok = false;
    int delayMs = qEnvironmentVariableIntValue("FRAMELIFT_TEST_EXIT_AFTER_MS", &ok);
    if (!ok)
    {
        return;
    }
    if (delayMs < 0)
    {
        delayMs = 0;
    }

    Log::Info("test exit requested after {} ms", delayMs);
    QTimer::singleShot(
        delayMs,
        [this]
        {
            AppEvent quit{};
            quit.type = AppEventType::Quit;
            Dispatch(quit);
        }
    );
}
#endif

void App::ResizeToVideo() const
{
    if (appWindow_->IsFullscreen())
    {
        return;
    }

    // Query the player's display size off-thread, then let the window size itself
    // to it (the one spot that must touch both player and window).
    player_->GetDisplaySizeAsync(
        [](const DisplaySize* size, bool ok, void* ud)
        {
            const auto* s = static_cast<AsyncSelf*>(ud);
            const App* self = s->app;
            delete s;
            if (!ok || !size)
            {
                return;
            }
            // Cap the auto-sized window at 80% of the usable screen.
            constexpr float kMaxDisplayRatio = 0.8f;
            self->appWindow_->ResizeToVideo(
                static_cast<int>(size->width), static_cast<int>(size->height), kMaxDisplayRatio
            );
        },
        new AsyncSelf{this}
    );
}

// ── Event loop ────────────────────────────────────────────────────────────────

int App::Run()
{
    // Hand the command line to modules (all loaded and subscribed by now), then
    // open the first positional file/URL argument — the same request drag-drop and
    // the Open-File dialog publish, so Playlist/RemoteStream route it by scheme.
    moduleCtx_->Publish<CliCommandEvent>({cliArgc_, cliArgv_});
    if (const std::string target = ParseOpenTarget(cliArgc_, cliArgv_); !target.empty())
    {
        moduleCtx_->Publish<OpenFileRequestEvent>({target.c_str(), true});
    }

    FRAMELIFT_PERF_END("app-start");
#if FRAMELIFT_BUILD_LAUNCH_TESTS
    ScheduleTestExitIfRequested();
#endif

    // Qt owns the loop now: show the window and run QGuiApplication::exec(). The
    // demand-driven semantics are preserved by scheduling repaints (QQuickWindow::update)
    // only on real change — input events, player worker wakeups — so the GPU idles
    // otherwise. The window paints inside the scene-graph render pass via RenderVideo().
    return appWindow_->RunEventLoop();
}
