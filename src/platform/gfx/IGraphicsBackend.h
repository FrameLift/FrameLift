#pragma once

#include <cstdint>
#include <memory>

#include "GraphicsApi.h"
#include "IVideoRenderer.h"
#include "VulkanDeviceInfo.h"

struct SDL_Window;

// Host-internal abstraction over the graphics presentation API (OpenGL today, Vulkan
// planned). It owns everything API-specific behind the window: the GL context / Vulkan
// device, buffer presentation, vsync, and the full Dear ImGui backend lifecycle
// (all imgui_impl_* calls). SdlAppWindow owns the SDL_Window and event loop and
// delegates the rendering surface to one of these.
//
// Not part of the plugin ABI — signatures may evolve as the Vulkan backend lands.
// Implementations may #include <SDL3/SDL.h> and imgui_impl_*.h (same allowance as
// SdlAppWindow). All methods run on the host's main / render thread.
class IGraphicsBackend
{
public:
    virtual ~IGraphicsBackend() = default;

    // Called before SDL_CreateWindow: set any API-specific SDL attributes (e.g. the GL
    // context version). Returns the extra SDL_WindowFlags to OR into the creation flags
    // (SDL_WINDOW_OPENGL / SDL_WINDOW_VULKAN).
    virtual uint64_t PreWindowCreate() = 0;

    // Called once the window exists: create the GL context / Vulkan device + swapchain
    // and make it current. The backend retains the window for present/show.
    virtual void OnWindowCreated(SDL_Window* window) = 0;

    // Destroy the API context/device. Called before the SDL_Window is destroyed.
    virtual void Shutdown() = 0;

    // Human-readable name of the active API ("OpenGL" / "Vulkan"), for diagnostics.
    [[nodiscard]] virtual const char* Name() const = 0;

    // True when the active graphics adapter is NVIDIA. Host-internal diagnostic/
    // settings hint used to expose CUDA-only decode modes only when they can work.
    [[nodiscard]] virtual bool HasNvidiaAdapter() const noexcept
    {
        return false;
    }

    // Create the video renderer paired with this backend (GlVideoRenderer for the GL
    // backend, VulkanVideoRenderer for Vulkan). The player owns the returned renderer
    // and calls IVideoRenderer::Init(this) on it.
    [[nodiscard]] virtual std::unique_ptr<IVideoRenderer> CreateVideoRenderer() = 0;

    // Upload a tightly packed RGBA8 image and return an ImGui-usable texture handle
    // (ImTextureID value): a GL texture name for the GL backend, a VkDescriptorSet for
    // Vulkan. Used by UIContextImpl for plugin icons (UIContext::LoadTexture*). The
    // backend owns the resource and frees it on shutdown. Returns 0 on failure.
    [[nodiscard]] virtual uintptr_t CreateUiTexture(const unsigned char* rgba, int w, int h) = 0;

    // Fill `out` with the live Vulkan instance/device/queues so the FFmpeg Vulkan
    // hwaccel can WRAP this device for zero-copy decode (Phase 3, #18). Returns false
    // for non-Vulkan backends (the default) — the caller then uses the CPU-RGBA8 path.
    // Stays Vulkan-type-free (VulkanDeviceInfo is a neutral POD) so the FFmpeg side can
    // call it without pulling in volk.
    [[nodiscard]] virtual bool GetVulkanDeviceInfo(VulkanDeviceInfo& out) const noexcept
    {
        (void)out;
        return false;
    }

    // ── Presentation ──────────────────────────────────────────────────────────
    [[nodiscard]] virtual void* GetProcAddr(const char* name) const = 0;
    // Begin a frame: acquire/clear the render target. Returns false if the frame
    // should be skipped (e.g. the Vulkan swapchain is mid-recreation). GL always
    // succeeds. Both the video renderer and the ImGui pass then record into the frame
    // before SwapBuffers() presents it.
    virtual bool BeginFrame() = 0;
    virtual void SwapBuffers() = 0;
    virtual void SetVSync(bool enabled) = 0;

    // ── Dear ImGui backend lifecycle (owns all imgui_impl_* calls) ────────────
    // The neutral ImGui context (CreateContext, style, io flags) is owned by the
    // caller; these wire up and drive the platform+renderer backends around it.
    virtual void ImGuiInitBackends() = 0;
    virtual void ImGuiShutdownBackends() = 0;
    // New-frame for the platform + renderer backends (the caller then calls
    // ImGui::NewFrame()).
    virtual void ImGuiNewFrame() = 0;
    // Render the current main-viewport draw data (the caller has already called
    // ImGui::Render()) into the active frame.
    virtual void ImGuiRenderDrawData() = 0;
    // Render/present secondary ImGui platform windows after the main frame has been
    // submitted/presented. This keeps Vulkan multi-viewport backend submits out of
    // the middle of the main swapchain frame.
    virtual void ImGuiRenderPlatformWindows() = 0;
    // Forward a native event to the ImGui platform backend. e is a const SDL_Event*.
    virtual void ImGuiProcessEvent(const void* sdlEvent) = 0;
};

// Create the presentation backend for `api`. Phase 1 implements OpenGL only; any other
// value logs a warning and falls back to OpenGL.
std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(GraphicsApi api);
