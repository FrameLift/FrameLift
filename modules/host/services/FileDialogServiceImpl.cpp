#include "FileDialogServiceImpl.h"
#include "CoreSettings.h"
#include "Settings.h"
#include <framelift/platform/IAppWindow.h>

#include <SDL3/SDL.h>
#include <memory>
#include <string>
#include <vector>

// FileDialogServiceImpl.cpp is intentionally SDL-aware: the platform picker API
// (SDL_ShowOpenFileDialog) is inherently backend-specific. All other code uses
// IAppWindow / the IFileDialog service.

// ── Internal payload ──────────────────────────────────────────────────────────
// Allocated per OpenFile call, ownership transferred to SDL, reclaimed in
// HandleEvent. Carries the window + event type so the (free) SDL callback can
// route the result back onto the main thread.

namespace
{
struct Payload
{
    void (*cb)(const char* path, bool ok, void* ud) = nullptr;
    void* ud = nullptr;
    IEventPump* events = nullptr;
    uint32_t eventType = 0;
    std::string path;
    std::vector<SDL_DialogFileFilter> sdlFilters; // kept alive until the callback fires
};

// ── SDL async callback (may be called from any thread) ────────────────────────

void SDLCALL SdlCallback(void* userdata, const char* const* fileList, int /*filter*/)
{
    auto* p = static_cast<Payload*>(userdata);
    if (fileList && fileList[0])
    {
        p->path = fileList[0];
    }
    p->events->PushCustomEvent(p->eventType, p);
}
} // namespace

// ── Public API ────────────────────────────────────────────────────────────────

void FileDialogServiceImpl::Init(IAppWindow* appWindow, IEventPump* events) noexcept
{
    appWindow_ = appWindow;
    events_ = events;
    eventType_ = events->RegisterCustomEventType();
}

void FileDialogServiceImpl::OpenFile(void (*cb)(const char* path, bool ok, void* ud), void* ud) noexcept
{
    if (!appWindow_)
    {
        return;
    }

    auto* p = new Payload{cb, ud, events_, eventType_, {}, {}};
    if (settings_)
    {
        const FilesSettings& files = settings_->Get<FilesSettings>();
        p->sdlFilters.push_back({"Video files", files.videoExtensions.c_str()});
        p->sdlFilters.push_back({"Image files", files.imageExtensions.c_str()});
    }

    SDL_ShowOpenFileDialog(
        SdlCallback, p, static_cast<SDL_Window*>(appWindow_->GetNativeHandle()),
        p->sdlFilters.empty() ? nullptr : p->sdlFilters.data(), static_cast<int>(p->sdlFilters.size()), nullptr, false
    );
    // Ownership transferred to the SDL callback; reclaimed in HandleEvent().
}

bool FileDialogServiceImpl::HandleEvent(const AppEvent& e) noexcept
{
    if (e.type != AppEventType::Custom)
    {
        return false;
    }
    const AppEvent::CustomPayload& cp = e.AsCustom();
    if (cp.eventType != eventType_)
    {
        return false;
    }

    const std::unique_ptr<Payload> p(static_cast<Payload*>(cp.userData1));
    if (p->cb)
    {
        p->cb(p->path.c_str(), !p->path.empty(), p->ud);
    }
    return true;
}
