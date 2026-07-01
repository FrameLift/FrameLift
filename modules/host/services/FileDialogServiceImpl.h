#pragma once
#include <framelift/AppEvent.h>
#include <framelift/platform/IFileDialog.h>
#include <cstdint>

class IAppWindow;
class IEventPump;
class Settings;

// Host file picker — the single file-dialog abstraction for both the host and
// plugins. Implements the plugin-facing IFileDialog ABI and adds host-only
// Init/HandleEvent for the async-result plumbing. The implementation
// (FileDialogServiceImpl.cpp) drives a native Qt QFileDialog and posts the result
// back through the event pump so the callback never fires reentrantly.
class FileDialogServiceImpl final : public IFileDialog
{
public:
    explicit FileDialogServiceImpl(const Settings* settings) : settings_(settings)
    {
    }

    // Call once after the window is created — registers the custom event type used
    // to deliver async picker results back onto the main thread. Takes the window
    // (for the native handle) and the event pump (for the custom event).
    void Init(IAppWindow* appWindow, IEventPump* events) noexcept;

    // Call once per frame from the event loop. Returns true if the event was a
    // picker result (consumed); fires the pending callback.
    bool HandleEvent(const AppEvent& e) noexcept;

    // IFileDialog — opens the native open-file picker with video+image filters
    // taken from settings. cb(path, ok, ud) fires on the main thread.
    void OpenFile(void (*cb)(const char* path, bool ok, void* ud), void* ud) noexcept override;

private:
    const Settings* settings_;
    IAppWindow* appWindow_ = nullptr;
    IEventPump* events_ = nullptr;
    uint32_t eventType_ = 0;
};
