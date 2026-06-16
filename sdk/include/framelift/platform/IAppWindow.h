#pragma once

#include <cstdint>
#include <framelift/AppEvent.h>

// Window-relative rectangle in pixels.
struct Rect
{
    int x = 0, y = 0, w = 0, h = 0;
};

// Pure interface for the platform window, event loop, and UI backend.
// SdlAppWindow is the ONLY file allowed to #include <SDL3/SDL.h> or imgui_impl_*.h.
class IAppWindow
{
public:
    static constexpr const char* InterfaceId = "framelift.IAppWindow";
    virtual ~IAppWindow() = default;

    // ── Window ────────────────────────────────────────────────────────────────
    virtual void GetSize(int& w, int& h) const noexcept = 0;
    virtual void SetSize(int w, int h) noexcept = 0;
    [[nodiscard]] virtual bool IsFullscreen() const noexcept = 0;
    virtual void SetFullscreen(bool fs) noexcept = 0;
    [[nodiscard]] virtual Rect GetDisplayUsableBounds() const noexcept = 0;
    [[nodiscard]] virtual void* GetNativeHandle() const noexcept = 0;
    [[nodiscard]] virtual bool SetWindowIcon(const char* path) noexcept = 0;
    [[nodiscard]] virtual bool SetWindowIconFromMemory(const unsigned char* data, int size) noexcept = 0;
    virtual void SetTitle(const char* title) noexcept = 0;

    // ── Graphics backend / presentation ────────────────────────────────────────
    // Opaque handle to the active graphics backend (host-internal IGraphicsBackend*),
    // handed to IMediaPlayer::InitRender so the player can build its video renderer.
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

    // ── Events ────────────────────────────────────────────────────────────────
    [[nodiscard]] virtual bool WaitNextEvent(AppEvent& out, int timeoutMs) noexcept = 0;
    [[nodiscard]] virtual bool PollNextEvent(AppEvent& out) noexcept = 0;
    [[nodiscard]] virtual uint32_t RegisterCustomEventType() noexcept = 0;
    virtual void PushCustomEvent(uint32_t eventType, void* data1 = nullptr) noexcept = 0;
    virtual void PushRenderUpdate() noexcept = 0;
    virtual void PushPlayerWakeup() noexcept = 0;
    virtual void PushQuitEvent() noexcept = 0;

    // ── Platform paths ────────────────────────────────────────────────────────
    // Returns full length excl. NUL; pass buf=nullptr to query size.
    // GetPrefPath: user-writable config dir, trailing separator included.
    [[nodiscard]] virtual int GetPrefPath(const char* org, const char* app, char* buf, int cap) const noexcept = 0;
    // GetBasePath: directory containing the running executable, trailing separator included.
    [[nodiscard]] virtual int GetBasePath(char* buf, int cap) const noexcept = 0;

    // ── ImGui lifecycle ───────────────────────────────────────────────────────
    virtual void SetImGuiIniPath(const char* path) noexcept = 0;
    virtual void ImGuiInit() noexcept = 0;
    virtual void ImGuiShutdown() noexcept = 0;
    virtual void UIBeginFrame() noexcept = 0;
    virtual void UIEndFrame() noexcept = 0;
    virtual void ImGuiProcessEvent(const AppEvent& event) noexcept = 0;
};