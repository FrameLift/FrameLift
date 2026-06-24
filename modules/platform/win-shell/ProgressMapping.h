#pragma once

// Pure, platform-independent mapping logic for the Windows shell integration
// (taskbar progress + error-toast debounce). This header pulls in NO Win32/COM
// headers so it can be compiled and unit-tested on the Linux CI runner; all
// actual Win32 calls live in TaskbarProgress.cpp / ToastNotifier.cpp.

// Taskbar button progress state, mirroring ITaskbarList3's TBPFLAG set without
// depending on <shobjidl.h>. TaskbarProgress.cpp maps these to TBPF_* values.
enum class ProgressState
{
    NoProgress,    // TBPF_NOPROGRESS  – clear the bar (idle / EOF / disabled)
    Indeterminate, // TBPF_INDETERMINATE – marquee (playing, unknown length)
    Normal,        // TBPF_NORMAL      – green bar tracking position
    Paused,        // TBPF_PAUSED      – yellow bar
    Error,         // TBPF_ERROR       – red bar
};

// A snapshot of the playback state the taskbar reflects, assembled by WinShell
// from the media-event stream.
struct PlaybackSnapshot
{
    double timePos = 0.0;  // current position, seconds
    double duration = 0.0; // media duration, seconds (<= 0 ⇒ unknown / live)
    bool paused = false;
    bool idle = false;    // no file loaded
    bool eof = false;     // reached end of stream
    bool errored = false; // last file ended with EndFileReason::Error
};

// Progress as permille (0..1000) for SetProgressValue(completed, total=1000).
// Clamped to [0,1000]; returns 0 when the duration is unknown.
inline int ProgressPermille(const PlaybackSnapshot& s) noexcept
{
    if (s.duration <= 0.0)
    {
        return 0;
    }
    double ratio = s.timePos / s.duration;
    if (ratio < 0.0)
    {
        ratio = 0.0;
    }
    if (ratio > 1.0)
    {
        ratio = 1.0;
    }
    return static_cast<int>(ratio * 1000.0 + 0.5);
}

// Map a snapshot to the taskbar progress state. Precedence is deliberate:
// error wins over everything; a finished/idle player clears the bar; a playing
// file of unknown length shows a marquee; otherwise paused vs normal.
inline ProgressState MapState(const PlaybackSnapshot& s) noexcept
{
    if (s.errored)
    {
        return ProgressState::Error;
    }
    if (s.idle || s.eof)
    {
        return ProgressState::NoProgress;
    }
    if (s.duration <= 0.0)
    {
        return ProgressState::Indeterminate;
    }
    if (s.paused)
    {
        return ProgressState::Paused;
    }
    return ProgressState::Normal;
}

// Throttle predicate: only touch the taskbar when the rendered value or state
// actually changes, so continuous TimePos updates don't spam the shell.
inline bool ProgressChanged(int newPermille, ProgressState newState, int lastPermille, ProgressState lastState) noexcept
{
    return newPermille != lastPermille || newState != lastState;
}

// Debounce predicate for error notifications: suppress a repeat toast for the
// same file within a short window (rapid EndFile/Error churn on reconfig or
// auto-advance). A different file, or enough elapsed time, allows the toast.
inline bool ShouldNotifyError(bool sameFileAsLast, double secondsSinceLast, double windowSeconds = 1.5) noexcept
{
    if (!sameFileAsLast)
    {
        return true;
    }
    return secondsSinceLast >= windowSeconds;
}
