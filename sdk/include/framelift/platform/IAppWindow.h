#pragma once

#include <cstdint>
#include <framelift/AppEvent.h>

// Window-relative rectangle in pixels.
struct Rect
{
    int x = 0, y = 0, w = 0, h = 0;
};

// The platform window is exposed to plugins as a family of small, independently
// discovered capability interfaces rather than one god-interface. One host object
// (SdlAppWindow) implements them all and registers under each id; a consumer fetches
// only the facets it uses via ctx.GetService<T>(). Adding a new window capability is
// a NEW interface here, never an append to an existing one, so the ABI version and
// the vtable layout of every interface below stay frozen.
//
// SdlAppWindow is the ONLY file allowed to #include <SDL3/SDL.h> or imgui_impl_*.h.
// Its host-only surface — ImGui lifecycle and pref/base path resolution — is not on
// any interface here; only the host (which owns the concrete object) calls it.

// Window geometry, title, icon and fullscreen state.
class IAppWindow
{
public:
    static constexpr const char* InterfaceId = "framelift.IAppWindow";
    virtual ~IAppWindow() = default;

    virtual void GetSize(int& w, int& h) const noexcept = 0;
    virtual void SetSize(int w, int h) noexcept = 0;
    [[nodiscard]] virtual bool IsFullscreen() const noexcept = 0;
    virtual void SetFullscreen(bool fs) noexcept = 0;
    [[nodiscard]] virtual Rect GetDisplayUsableBounds() const noexcept = 0;
    [[nodiscard]] virtual void* GetNativeHandle() const noexcept = 0;
    [[nodiscard]] virtual bool SetWindowIcon(const char* path) noexcept = 0;
    [[nodiscard]] virtual bool SetWindowIconFromMemory(const unsigned char* data, int size) noexcept = 0;
    virtual void SetTitle(const char* title) noexcept = 0;
};

// Graphics backend handle + frame presentation.
class IGraphicsSurface
{
public:
    static constexpr const char* InterfaceId = "framelift.IGraphicsSurface";
    virtual ~IGraphicsSurface() = default;

    // Opaque handle to the active graphics backend (host-internal IGraphicsBackend*),
    // handed to IVideoOutput::InitRender so the player can build its video renderer.
    [[nodiscard]] virtual void* GetGraphicsBackend() const noexcept = 0;
    // Human-readable name of the *active* graphics API ("OpenGL" / "Vulkan"). Reflects
    // the real backend, including any fallback from the requested one. For diagnostics.
    [[nodiscard]] virtual const char* GetGraphicsBackendName() const noexcept = 0;
    // Begin the frame on the graphics backend (acquire the target surface / image).
    // Returns false if the frame should be skipped this iteration (e.g. the Vulkan
    // swapchain is being recreated); the caller then renders nothing and presents nothing.
    virtual bool BeginFrame() noexcept = 0;
    // End the frame: present the rendered image to the display.
    virtual void SwapBuffers() noexcept = 0;
    // Enable/disable vsync. On = present synced to the display refresh.
    virtual void SetVSync(bool enabled) noexcept = 0;
    // False while the window is minimized, hidden, or fully occluded — the host
    // skips rendering and idles on events instead of spinning the GPU.
    [[nodiscard]] virtual bool IsRenderable() const noexcept = 0;
};

// The platform event pump and custom-event injection.
class IEventPump
{
public:
    static constexpr const char* InterfaceId = "framelift.IEventPump";
    virtual ~IEventPump() = default;

    [[nodiscard]] virtual bool WaitNextEvent(AppEvent& out, int timeoutMs) noexcept = 0;
    [[nodiscard]] virtual bool PollNextEvent(AppEvent& out) noexcept = 0;
    [[nodiscard]] virtual uint32_t RegisterCustomEventType() noexcept = 0;
    virtual void PushCustomEvent(uint32_t eventType, void* data1 = nullptr) noexcept = 0;
    virtual void PushRenderUpdate() noexcept = 0;
    virtual void PushPlayerWakeup() noexcept = 0;
    virtual void PushQuitEvent() noexcept = 0;
};