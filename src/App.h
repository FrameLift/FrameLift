#pragma once

#include "FileDialogServiceImpl.h"
#include "PackageConfig.h"
#include "FocusManagerImpl.h"
#include "HotkeysImpl.h"
#include "JsonServiceImpl.h"
#include "ModuleContext.h"
#include "PackageLoader.h"
#include "ModuleRegistry.h"
#include "PlaybackControls.h"
#include "Settings.h"
#include "ThemeController.h"
#include "UIContextImpl.h"
#include <framelift/IRenderable.h>
#include <framelift/platform/IAppWindow.h>
#include <framelift/platform/IDirWatcher.h>
#include <framelift/platform/IMediaPlayer.h>
#include <memory>
#include <string>
#include <vector>

class SdlAppWindow;
class FFmpegPlayer;
class WinShell;

// Top-level application object. Owns all subsystems, drives the main loop,
// and co-ordinates rendering. Exactly one instance exists for the program lifetime.
// Has no compile-time knowledge of specific modules — every capability ships as a
// package DLL loaded at runtime from the packages/ directory.
class App
{
public:
    App(const char* title, int width, int height, int cliArgc = 0, const char* const* cliArgv = nullptr);
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
    void InitPlatform(const char* title, int width, int height, const std::string& prefDir, const std::string& settingsPath);
    void InitServices(const std::string& prefDir, const std::string& settingsPath);
    void LoadPackages();
    void BuildRenderables();

    void InitRender() const;
    void InitImGui() const;

    void Render();
    void ResizeToVideo() const;

    void DrainEvents(int timeoutMs);
    void Dispatch(const AppEvent& e);
    void DrainMediaEvents();
    void RenderFrame();

    // Process command line, forwarded from main(). main()'s argv stays valid for
    // the program lifetime, so storing the pointer is safe. Broadcast verbatim as
    // a CliCommandEvent at startup; the first positional arg also opens a file.
    int cliArgc_ = 0;
    const char* const* cliArgv_ = nullptr;

    std::unique_ptr<SdlAppWindow> appWindow_;
    // App always builds the concrete FFmpegPlayer: it needs the FFmpeg-only entry
    // points (ApplySettings, decode mode, ducking) that aren't on any of the split
    // playback interfaces, and it registers each of those interfaces as a service.
    std::unique_ptr<FFmpegPlayer> player_;
    FFmpegPlayer* ffmpeg_ = nullptr; // alias of player_.get(), kept for readability at call sites
    std::unique_ptr<IDirWatcher> dirWatcher_;

    bool pendingResize_ = false;
    bool running_ = true;

    Settings settings_;
    PackageConfig packageConfig_;
    std::string packagesPath_;
    FileDialogServiceImpl fileDialogService_{&settings_};
    FocusManagerImpl focus_;
    HotkeysImpl keys_;
    JsonServiceImpl jsonService_;
    UIContextImpl uiCtx_;
    ThemeController themeController_;

    std::unique_ptr<ModuleContext> moduleCtx_;
    std::unique_ptr<PlaybackControls> playbackControls_;
#if FRAMELIFT_MODULE_WIN_SHELL
    // Windows-only: taskbar playback progress + error toasts. Driven off the media
    // event stream in DrainMediaEvents; not part of the plugin registry.
    std::unique_ptr<WinShell> winShell_;
#endif
    PackageLoader packageLoader_;
    ModuleRegistry registry_;

    std::vector<IRenderable*> renderables_;
};
