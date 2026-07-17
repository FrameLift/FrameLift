#pragma once

#if FRAMELIFT_MODULE_AI
#include "AIService.h"
#endif
#include "FileDialogServiceImpl.h"
#if FRAMELIFT_MODULE_FRAME_SAMPLER
#include "FrameSamplerService.h"
#endif
#include "GraphicsApi.h"
#include "GraphicsInfoService.h"
#include "HostSettings.h"
#include "HotkeysImpl.h"
#include "JsonServiceImpl.h"
#include "MediaStoreImpl.h"
#include "ModuleContext.h"
#include "ModuleRegistry.h"
#include "PlaybackControls.h"
#include "PluginConfig.h"
#include "PluginLoader.h"
#include "VideoDecodeCaps.h"
#include <chrono>
#include <framelift/platform/IAppWindow.h>
#include <framelift/platform/IMediaPlayer.h>
#include <memory>
#include <string>
#include <vector>

class QtAppWindow;
class FFmpegPlayer;
class WinShell;

// Top-level application object. Owns all subsystems, drives the main loop,
// and co-ordinates rendering. Exactly one instance exists for the program lifetime.
// Has no compile-time knowledge of specific plugins; every user-facing capability
// ships as a plugin DLL/SO loaded at runtime from the plugins/ directory.
class App
{
public:
    App(const char* title, int width, int height, GraphicsApi graphicsApi, int cliArgc = 0,
        const char* const* cliArgv = nullptr);
    ~App();

    int Run();

private:
    // Owner cell for the async resize query: heap-allocated, passed as the opaque
    // user-data pointer, and deleted inside the trampoline once it fires.
    struct AsyncSelf
    {
        const App* app;
    };

    // ── Construction phases (run in order from the ctor) ──
    void InitPlatform(
        const char* title, int width, int height, GraphicsApi graphicsApi, const std::string& prefDir,
        const std::string& settingsPath
    );
    void InitServices(const std::string& prefDir, const std::string& settingsPath);
    // Reject a decode mode the machine can't honor. FL_ACCEL_MODE (explicit env
    // override) is fatal; a stale persisted playback.hwdecMode logs and falls back
    // to auto so the app still starts. Runs after InitServices (videoDecodeCaps_ up).
    void ValidateDecodeModeSelection();
    void LoadPlugins();
    void BuildPluginViews();

    // Wire the player's worker-thread wakeups to the window's queued signals (no GL).
    void SetupPlayerCallbacks();
    void ResizeToVideo() const;
    // Broadcast the one application-shutdown sequence. Safe to call from both the
    // quit-event path and after the Qt event loop returns.
    void BeginShutdown();

    void Dispatch(const AppEvent& e);
    void DrainMediaEvents();
#if FRAMELIFT_BUILD_LAUNCH_TESTS
    void ScheduleTestExitIfRequested();
#endif

    // The window's scene-graph video node calls this on the GUI thread with the target
    // framebuffer rect in device pixels (origin is nonzero when the fallback title bar
    // insets the video): lazily adopts Qt's GL context + builds the renderer on first
    // call, then draws the current frame letterboxed within the rect.
    void PrepareVideo(int fbX, int fbY, int fbW, int fbH);
    void RenderVideo(int fbX, int fbY, int fbW, int fbH);
    // Queued-signal handler: drain media events and apply any pending video-driven resize.
    void OnPlayerWakeup();

    // Process command line, forwarded from main(). main()'s argv stays valid for
    // the program lifetime, so storing the pointer is safe. Broadcast verbatim as
    // a CliCommandEvent at startup; the first positional arg also opens a file.
    int cliArgc_ = 0;
    const char* const* cliArgv_ = nullptr;

    // ── Teardown contract (members destruct in REVERSE declaration order) ──
    // The order below is load-bearing; do not reorder without re-checking ~App:
    //   • pluginLoader_ is declared last among the owning members, so it destructs
    //     (FreeLibrary) only after every object that may live in a plugin DLL —
    //     module instances in registry_, plugin view models, moduleCtx_ — is gone.
    //     A plugin module may still call ctx_->GetService() from its destructor.
    //   • moduleCtx_ is declared before pluginLoader_ (outlives it on destruction is
    //     handled in ~App) and after the services it registers, so those services
    //     stay alive while modules tear down.
    //   • player_ and appWindow_ own GPU/render resources; ~App resets them explicitly
    //     in the right order (player render context before the GL context) rather than
    //     relying on declaration order alone.
    std::unique_ptr<QtAppWindow> appWindow_;
    // App always builds the concrete FFmpegPlayer: it needs the FFmpeg-only entry
    // points (ApplySettings, decode mode, ducking) that aren't on any of the split
    // playback interfaces, and it registers each of those interfaces as a service.
    std::unique_ptr<FFmpegPlayer> player_;
    FFmpegPlayer* ffmpeg_ = nullptr; // alias of player_.get(), kept for readability at call sites

    bool pendingResize_ = false;
    bool shutdownStarted_ = false;

    // First-frame guard: Qt's scene-graph GL context only exists once the SG initializes,
    // so the backend's context adoption + the player's renderer build are deferred to the
    // first RenderVideo() (where the GL context is current).
    bool renderInit_ = false;

    HostSettings settings_;
    PluginConfig pluginConfig_;
    std::string settingsPath_;
    std::string pluginsPath_;
    bool pluginConfigDirty_ = false;
    bool settingsFileExistedAtLaunch_ = false;
    FileDialogServiceImpl fileDialogService_{&settings_};
    HotkeysImpl keys_;
    JsonServiceImpl jsonService_;
    std::unique_ptr<MediaStoreImpl> mediaStore_; // needs prefDir, so built in InitServices
#if FRAMELIFT_MODULE_AI
    std::unique_ptr<AIService> aiService_;
#endif
#if FRAMELIFT_MODULE_FRAME_SAMPLER
    // Off-playback FFmpeg frame decode for indexing/analysis plugins (IFrameSampler).
    std::unique_ptr<FrameSamplerService> frameSampler_;
#endif
    std::unique_ptr<GraphicsInfoService> graphicsInfo_;
    std::unique_ptr<VideoDecodeCaps> videoDecodeCaps_;

    std::unique_ptr<ModuleContext> moduleCtx_;
    std::unique_ptr<PlaybackControls> playbackControls_;
#if FRAMELIFT_MODULE_WIN_SHELL
    // Windows-only: Qt-backed playback error notifications. Driven off the media
    // event stream in DrainMediaEvents; not part of the plugin registry.
    std::unique_ptr<WinShell> winShell_;
#endif
    PluginLoader pluginLoader_;
    ModuleRegistry registry_;
};
