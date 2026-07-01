#pragma once

#include <cstdint>
#include <framelift/AppEvent.h>

// The platform window is exposed to plugins as a family of small, independently
// discovered capability interfaces rather than one god-interface. One host object
// (QtAppWindow) implements the facets and registers under each id; a consumer
// fetches only the facets it uses via ctx.GetService<T>().
//
// QtAppWindow owns the host-only window/event-loop surface; only the host (which
// owns the concrete object) calls methods outside the ABI interfaces here.

// Plugin-visible window title and fullscreen state.
class IAppWindow
{
public:
    static constexpr const char* InterfaceId = "framelift.IAppWindow";
    virtual ~IAppWindow() = default;

    [[nodiscard]] virtual bool IsFullscreen() const noexcept = 0;
    virtual void SetFullscreen(bool fs) noexcept = 0;
    virtual void SetTitle(const char* title) noexcept = 0;
};

// Custom-event injection into the Qt-owned event loop.
class IEventPump
{
public:
    static constexpr const char* InterfaceId = "framelift.IEventPump";
    virtual ~IEventPump() = default;

    [[nodiscard]] virtual uint32_t RegisterCustomEventType() noexcept = 0;
    virtual void PushCustomEvent(uint32_t eventType, void* data1 = nullptr) noexcept = 0;
    virtual void PushQuitEvent() noexcept = 0;
};
