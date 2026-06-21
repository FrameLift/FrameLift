#pragma once
#include <framelift/Abi.h>
#include <type_traits>

// Module bootstrap context. Passed into IModule::Install(). Deliberately tiny: it
// only carries the service registry and the pub/sub bus — the two things every
// module needs before it can discover anything else. All host functionality
// (settings, keybind/page registration, the package catalogue, fonts, paths, media
// playback, the window) is reached through capability services fetched with
// GetService<T>(); see <framelift/services.h> and <framelift/platform.h>.
//
// This is a Tier-1 interface: changing its vtable bumps FRAMELIFT_ABI_VERSION. It is
// frozen precisely so new host capabilities can ship as new services without ever
// touching it. All virtuals use a stable C-compatible ABI — no STL types cross the
// boundary. Use the non-virtual template helpers (GetService, RegisterService,
// Publish, Subscribe) for ergonomic access; they compile into the module.
class IModuleContext
{
public:
    static constexpr const char* InterfaceId = "framelift.IModuleContext";
    virtual ~IModuleContext() = default;

    // ── Service registry ───────────────────────────────────────────────────────
    // Keys are InterfaceId strings (e.g. T::InterfaceId).
    virtual void* GetServiceRaw(const char* id) const noexcept = 0;
    virtual void RegisterServiceRaw(const char* id, void* svc) noexcept = 0;

    // ── Pub/sub ────────────────────────────────────────────────────────────────
    // Events are keyed by EventId (e.g. TEvent::EventId).
    // cleanup(ud) is called when subscriptions are cleared (module unload).
    virtual void SubscribeRaw(
        const char* eventId, void (*cb)(const void* event, void* ud), void* ud, void (*cleanup)(void* ud) = nullptr
    ) noexcept = 0;
    virtual void PublishRaw(const char* eventId, const void* payload) noexcept = 0;

    // ── Convenience templates (non-virtual — compiled into the module) ──────────

    template <typename T>
    T* GetService() const noexcept
    {
        return static_cast<T*>(GetServiceRaw(std::remove_const_t<T>::InterfaceId));
    }

    template <typename T, typename... Us>
    void RegisterService(T* svc) noexcept
    {
        RegisterServiceRaw(std::remove_const_t<T>::InterfaceId, const_cast<std::remove_const_t<T>*>(svc));
        (..., RegisterServiceRaw(std::remove_const_t<Us>::InterfaceId, static_cast<std::remove_const_t<Us>*>(svc)));
    }

    template <typename TEvent>
    void Publish(const TEvent& event) noexcept
    {
        PublishRaw(TEvent::EventId, &event);
    }
};
