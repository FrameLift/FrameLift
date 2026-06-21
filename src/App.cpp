#include "App.h"
#include "Cli.h"
#include "IconData.h"
#include "CoreSettings.h"
#include "PlaybackSettings.h"
#include "GraphicsApi.h"
#include "GraphicsSettings.h"
#include "IGraphicsBackend.h"
#include "DirWatcher.h"
#include "FFmpegPlayer.h"
#include "SdlAppWindow.h"
#if FRAMELIFT_MODULE_WIN_SHELL
#include "WinShell.h"
#endif
#include <framelift/Log.h>
#include <framelift/Events.h>
#include <framelift/IModule.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <algorithm>
#include <ranges>
#include <unordered_set>

// ── Constructor / Destructor ──────────────────────────────────────────────────

App::App(const char* title, const int width, const int height, const int cliArgc, const char* const* cliArgv)
    : cliArgc_(cliArgc), cliArgv_(cliArgv), player_(std::make_unique<FFmpegPlayer>()),
      dirWatcher_(CreateDirWatcher())
{
    ffmpeg_ = player_.get();

    // Resolve the pref dir up front: the graphics backend — and thus the SDL window
    // flag (SDL_WINDOW_OPENGL vs SDL_WINDOW_VULKAN) — is fixed at window-creation
    // time, so graphics.backend must be loaded from settings before InitPlatform.
    char prefBuf[512] = {};
    (void)SdlAppWindow::ResolvePrefPath("", "FrameLift", prefBuf, sizeof(prefBuf));
    const std::string prefDir = prefBuf;
    const std::string settingsPath = prefDir.empty() ? "settings.ini" : prefDir + "settings.ini";
    packagesPath_ = prefDir.empty() ? "packages.ini" : prefDir + "packages.ini";

    InitPlatform(title, width, height, prefDir, settingsPath);
    InitServices(prefDir, settingsPath);

    // The ContextMenu module owns the right-click menu: it registers the ContextMenu
    // service that other modules extend, and assembles its core items plus their
    // sections on its first frame.
    LoadPackages();

    BuildRenderables();
}

App::~App()
{
    appWindow_->ImGuiShutdown();

    // Clear all DLL-owned lambdas before packageLoader_ calls FreeLibrary.
    // packageLoader_ destructs after this body (declared after moduleCtx_),
    // so modules can still call ctx_->GetService() in their destructors.
    if (moduleCtx_)
    {
        moduleCtx_->ClearSubscriptions();
    }
    keys_.Clear();

    player_.reset(); // destroy the player's render context before the GL context is torn down
}

// ── Construction phases ─────────────────────────────────────────────────────────

void App::InitPlatform(const char* title, const int width, const int height, const std::string& prefDir,
                       const std::string& settingsPath)
{
    // App owns the Settings instance; load it before any module sees it.
    settings_.Load(settingsPath);

    // User package enablement manifest (opt-out): load before packages are scanned.
    packageConfig_.Load(packagesPath_);

    appWindow_ =
        std::make_unique<SdlAppWindow>(title, width, height, GraphicsApiFromString(settings_.Get<GraphicsSettings>().backend));

    // Let the UI context create package-icon textures through the active backend.
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
    // NewFrame has not run yet) and seed the controller's snapshot.
    themeController_.ApplyInitial(settings_.Get<ThemeSettings>());

    fileDialogService_.Init(appWindow_.get(), appWindow_.get());
}

void App::InitServices(const std::string& prefDir, const std::string& settingsPath)
{
    moduleCtx_ = std::make_unique<ModuleContext>(prefDir, &settings_, settingsPath, &packageConfig_, packagesPath_);

    // The one FFmpegPlayer is registered under each capability interface it implements.
    // Register each separately: the variadic RegisterService can't sibling-cast a
    // concrete pointer across unrelated bases. Plugins fetch only the facets they use.
    moduleCtx_->RegisterService<IMediaPlayback>(player_.get());
    moduleCtx_->RegisterService<IMediaProperties>(player_.get());
    moduleCtx_->RegisterService<IVideoOutput>(player_.get());
    moduleCtx_->RegisterService<IAudioControl>(player_.get());
    moduleCtx_->RegisterService<ISubtitleControl>(player_.get());
    // The one SdlAppWindow is registered under each window facet it implements.
    moduleCtx_->RegisterService<IAppWindow>(appWindow_.get());
    moduleCtx_->RegisterService<IGraphicsSurface>(appWindow_.get());
    moduleCtx_->RegisterService<IEventPump>(appWindow_.get());
    moduleCtx_->RegisterService<IDirWatcher>(dirWatcher_.get());
    moduleCtx_->RegisterService<Hotkeys>(&keys_);
    moduleCtx_->RegisterService<FocusManager>(&focus_);
    moduleCtx_->RegisterService<IFileDialog>(&fileDialogService_);
    moduleCtx_->RegisterService<IJson>(&jsonService_);

    // Controllers own their own event-bus wiring (settings re-apply, audio ducking,
    // theme reaction) so App holds no subscriptions.
    playbackControls_ =
        std::make_unique<PlaybackControls>(
            keys_, settings_, *ffmpeg_, *appWindow_, *appWindow_, *appWindow_, fileDialogService_, *moduleCtx_
        );
    playbackControls_->Connect();
    themeController_.Connect(*moduleCtx_, settings_);

#if FRAMELIFT_MODULE_WIN_SHELL
    // Windows shell integration consumes the same services/events; wire it after
    // they're registered so its Connect() can resolve them.
    winShell_ = std::make_unique<WinShell>(appWindow_->GetWin32Hwnd());
    winShell_->Connect(*moduleCtx_);
#endif
}

// ── Package loading ─────────────────────────────────────────────────────────────

void App::LoadPackages()
{
    // Load every package present in the packages/ subdirectory. Per-module enablement
    // comes from packages.ini (keyed by module id); the loader resolves dependencies
    // and load order from embedded metadata, then instantiates each enabled module.
    // Each module registers its own context menu sections etc. during Install().
    char baseBuf[512] = {};
    (void)appWindow_->GetBasePath(baseBuf, sizeof(baseBuf));
    const std::string packagesDir = std::string(baseBuf) + "packages/";
    packageLoader_.LoadAll(packagesDir, packageConfig_.DisabledIds());

    // What actually got instantiated this session, by package id and module id.
    std::unordered_set<std::string> loadedPackageIds;
    std::unordered_set<std::string> loadedModuleIds;
    for (const auto& p : packageLoader_.Packages())
    {
        loadedPackageIds.insert(p.name);
        for (const auto& m : p.modules)
        {
            loadedModuleIds.insert(m.moduleId);
        }
    }

    // Build the catalogue from every discovered package (loaded or not) so the
    // settings UI can list and toggle each module. Populate it before Install() so a
    // module may enumerate peers during it.
    std::vector<std::string> discoveredModuleIds;
    for (auto& pkg : PackageLoader::DiscoverAvailable(packagesDir))
    {
        ModuleContext::PackageCatalogEntry entry;
        entry.id = pkg.packageId;
        entry.displayName = std::move(pkg.displayName);
        entry.version[0] = pkg.version[0];
        entry.version[1] = pkg.version[1];
        entry.version[2] = pkg.version[2];
        entry.publisher = std::move(pkg.publisher);
        entry.description = std::move(pkg.description);
        entry.loaded = loadedPackageIds.contains(pkg.packageId);
        for (auto& mod : pkg.modules)
        {
            ModuleContext::ModuleCatalogEntry me;
            discoveredModuleIds.push_back(mod.id);
            // enabled=false ⇒ user-disabled (unchecked in the UI); enabled=true but
            // not loaded ⇒ resolver-rejected/failed (surfaces as a load failure).
            me.enabled = packageConfig_.IsEnabled(mod.id);
            me.loaded = loadedModuleIds.contains(mod.id);
            me.id = std::move(mod.id);
            me.name = std::move(mod.name);
            me.description = std::move(mod.description);
            entry.modules.push_back(std::move(me));
        }
        moduleCtx_->AddPackage(std::move(entry));
    }

    // Install each loaded module (every module of every loaded package).
    for (auto& p : packageLoader_.Packages())
    {
        for (auto& m : p.modules)
        {
            registry_.Add(m.module, *moduleCtx_);
        }
    }

    // Refresh the manifest so it lists every discovered module (new ones default to
    // enabled), keeping packages.ini a complete, hand-editable record.
    packageConfig_.EnsureKnown(discoveredModuleIds);
    packageConfig_.Save(packagesPath_);

    registry_.BindHotkeys(keys_);
    playbackControls_->Bind();

    // Materialize module settings + keybinds on first run: modules populate their
    // ModuleSettingsImpl caches during Install(), but those reach disk only via
    // SaveSettings(). The startup Settings::Save (InitPlatform, before packages)
    // writes host sections only, so without this flush module sections appear only
    // after the user presses Save in the Settings menu.
    moduleCtx_->SaveSettings();
}

// ── Renderables ───────────────────────────────────────────────────────────────

void App::BuildRenderables()
{
    // Collect (order, renderable) pairs from packages and sort. The context menu is
    // itself a module (renderOrder 30), so it sorts in alongside the rest.
    using OrderedR = std::pair<int, IRenderable*>;
    std::vector<OrderedR> items;

    for (const auto& p : packageLoader_.Packages())
    {
        for (const auto& m : p.modules)
        {
            if (m.renderable)
            {
                items.emplace_back(m.renderOrder, m.renderable);
            }
        }
    }

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
    themeController_.ApplyPending(settings_.Get<ThemeSettings>());

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
    appWindow_->ImGuiRenderPlatformWindows();
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
            playbackControls_->SetPlayerIdle(ev.property.value.flag != 0);
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
    appWindow_->ImGuiProcessEvent(e);
    switch (e.type) // NOLINT(clang-diagnostic-switch-enum)
    {
    case AppEventType::Quit:
        // Let modules handle their own Quit cleanup (e.g. Playlist::FlushCurrentPos).
        registry_.OnEvent(e);
        registry_.OnShutdown();
#if FRAMELIFT_MODULE_WIN_SHELL
        if (winShell_)
        {
            winShell_->OnShutdown();
        }
#endif
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
        if (auto* f = focus_.Focused())
        {
            if (auto* handler = static_cast<IEventHandler*>(f->QueryInterface(IEventHandler::InterfaceId)))
            {
                if (handler->OnEvent(e))
                {
                    return;
                }
            }
        }
        // ReSharper disable once CppExpressionWithoutSideEffects
        keys_.Handle(e);
        return;
    }

    registry_.OnEvent(e);
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
            const float ratio = self->settings_.Get<GeneralSettings>().maxDisplayRatio;
            self->appWindow_->ResizeToVideo(static_cast<int>(size->width), static_cast<int>(size->height), ratio);
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
        const int timeoutMs = !renderable ? 100 : (settings_.Get<PlaybackSettings>().videoSync ? 0 : 4);
        DrainEvents(timeoutMs);
        if (running_ && appWindow_->IsRenderable())
        {
            RenderFrame();
        }
    }
    return 0;
}
