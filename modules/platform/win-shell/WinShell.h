#pragma once

#include "ProgressMapping.h"
#include <framelift/platform/IMediaPlayer.h>
#include <chrono>
#include <memory>
#include <string>

class IModuleContext;
class ISettingsStore;
class UIContext;
class TaskbarProgress;
class ToastNotifier;

// Windows-only host integration: mirrors playback progress on the taskbar button
// and raises an OS toast when a file fails to play. A plain host class (owned by
// App, like PlaybackControls) — NOT a plugin/ModuleBase — driven off the media
// event stream App already drains. This header is free of <windows.h>; all Win32
// lives behind the TaskbarProgress / ToastNotifier seams.
class WinShell
{
public:
    explicit WinShell(void* hwnd); // hwnd is an HWND from SdlAppWindow::GetWin32Hwnd()
    ~WinShell();

    WinShell(const WinShell&) = delete;
    WinShell& operator=(const WinShell&) = delete;

    // Observe the playback properties we reflect, load the notifications toggle,
    // and register the "Notifications" settings page.
    void Connect(IModuleContext& ctx);

    // Called from App::DrainMediaEvents for every media event. Updates the snapshot
    // and pushes a throttled taskbar update; raises an error toast on EndFile/Error.
    void OnMediaEvent(const MediaEvent& e);

    // Clear the taskbar bar before the window goes away.
    void OnShutdown();

    // Settings page hooks (registered via ISettingsRegistry).
    void RenderSettings(UIContext& ctx);
    void ApplySettings();

private:
    void PushIfChanged();

    std::unique_ptr<TaskbarProgress> taskbar_;
    std::unique_ptr<ToastNotifier> toast_;
    ISettingsStore* store_ = nullptr;

    PlaybackSnapshot snap_{};
    int lastPermille_ = -1;
    ProgressState lastState_ = ProgressState::NoProgress;

    bool notifyEnabled_ = true; // mirror of settings key "winshell.notifications"

    // Latest opened file (from FileOpenedEvent), used as the toast body text.
    std::string currentFile_;

    // Error-toast debounce state.
    std::string lastErrorFile_;
    std::chrono::steady_clock::time_point lastErrorTime_{};
    bool hasLastError_ = false;
};
