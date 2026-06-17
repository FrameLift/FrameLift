#pragma once

#include "FileDialogServiceImpl.h"
#include "FocusManagerImpl.h"
#include "HotkeysImpl.h"
#include "PluginContext.h"
#include "PluginLoader.h"
#include "PluginRegistry.h"
#include "Services.h"
#include "Settings.h"
#include "ui/ContextMenuImpl.h"
#include "ui/UIContextImpl.h"
#include <framelift/IRenderable.h>
#include <framelift/platform/IAppWindow.h>
#include <framelift/platform/IDirWatcher.h>
#include <framelift/platform/IMediaPlayer.h>
#include <chrono>
#include <memory>
#include <vector>

// Top-level application object. Owns all subsystems, drives the main loop,
// and co-ordinates rendering. Exactly one instance exists for the program lifetime.
// Has no compile-time knowledge of specific plugins — all plugins are loaded
// at runtime as DLLs from the plugins/ directory.
class App
{
public:
    App(const char* title, int width, int height, int cliArgc = 0, const char* const* cliArgv = nullptr);
    ~App();

    // Return type globally-qualified: the member name `Services` otherwise shadows
    // the class `Services` in this scope (GCC 14 -Wchanges-meaning), same reason the
    // static members below are declared `::Services` / `::PluginRegistry`.
    static ::Services& Services();
    static PluginRegistry& Registry();

    int Run();

private:
    void InitRender() const;
    void InitImGui() const;
    void BindPlayerHotkeys();

    void TogglePauseAction() const;
    void OpenFileAction();
    void AdjustVolumeAndNotify(int delta) const;

    void Render();
    void ResizeToVideo() const;

    void DrainEvents(int timeoutMs);
    void Dispatch(const AppEvent& e);
    void DrainMediaEvents();
    void RenderFrame();

    void LoadPlugins();
    void BuildContextMenu();
    void BuildRenderables();
    void PulseAudioDucking();
    void RefreshAudioDucking();

    // Process command line, forwarded from main(). main()'s argv stays valid for
    // the program lifetime, so storing the pointer is safe. Broadcast verbatim as
    // a CliCommandEvent at startup; the first positional arg also opens a file.
    int cliArgc_ = 0;
    const char* const* cliArgv_ = nullptr;

    std::unique_ptr<IAppWindow> appWindow_;
    std::unique_ptr<IMediaPlayer> player_;
    std::unique_ptr<IDirWatcher> dirWatcher_;

    bool pendingResize_ = false;
    bool running_ = true;
    // Mirrors the player's idle state (true = no file loaded); tracked in
    // DrainMediaEvents() so TogglePauseAction can decide without asking plugins.
    bool playerIdle_ = true;
    bool audioDucked_ = false;
    std::chrono::steady_clock::time_point audioDuckUntil_{};
    AudioPreferences runtimeAudioPrefs_{};

    // Theme application is deferred: the settings-change callback fires mid-frame
    // (during SettingsMenu's Save inside Render), so it only sets these flags and
    // the work runs at the top of the next Render(), outside any ImGui frame.
    bool themeStyleDirty_ = false;
    bool fontAtlasDirty_ = false;

    // Last-applied theme values, used to detect which aspect changed on Save.
    struct AppliedTheme
    {
        std::string preset;
        std::string accentColor;
        std::string fontFile;
        float fontSize = 0.f;
    } appliedTheme_;

    Settings settings_;
    FileDialogServiceImpl fileDialogService_{&settings_};
    FocusManagerImpl focus_;
    HotkeysImpl keys_;
    ContextMenuImpl contextMenu_;
    UIContextImpl uiCtx_;

    std::unique_ptr<PluginContext> pluginCtx_;
    PluginLoader pluginLoader_;

    std::vector<IRenderable*> renderables_;

    static ::Services services_;
    static ::PluginRegistry registry_;
};
