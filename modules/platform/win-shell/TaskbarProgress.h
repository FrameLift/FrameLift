#pragma once

#include "ProgressMapping.h"

// Thin seam over the Windows taskbar progress API (ITaskbarList3). The header is
// free of <windows.h>/<shobjidl.h> so WinShell.cpp (and the CI test build) never
// pull in Win32; the COM lifetime lives entirely in TaskbarProgress.cpp.
class TaskbarProgress
{
public:
    explicit TaskbarProgress(void* hwnd) noexcept; // hwnd is an HWND (or nullptr)
    ~TaskbarProgress();

    TaskbarProgress(const TaskbarProgress&) = delete;
    TaskbarProgress& operator=(const TaskbarProgress&) = delete;

    // Set the filled fraction (0..1000). No-op unless the state is a bar that
    // shows a value (Normal/Paused/Error).
    void SetValue(int permille) noexcept;
    // Set the bar style (green/yellow/red/marquee/none).
    void SetState(ProgressState state) noexcept;
    // Clear the bar (TBPF_NOPROGRESS). Safe to call on shutdown.
    void Clear() noexcept;

private:
    void* hwnd_ = nullptr;
    void* taskbar_ = nullptr; // ITaskbarList3*
    bool comInitialized_ = false;
};
