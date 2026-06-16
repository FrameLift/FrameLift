#pragma once

#include <cstdint>
#include <memory>

#include "GraphicsApi.h"

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

    // ── Presentation ──────────────────────────────────────────────────────────
    [[nodiscard]] virtual void* GetProcAddr(const char* name) const = 0;
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
    // Render the current draw data (the caller has already called ImGui::Render())
    // and present any multi-viewport platform windows.
    virtual void ImGuiRenderDrawData() = 0;
    // Forward a native event to the ImGui platform backend. e is a const SDL_Event*.
    virtual void ImGuiProcessEvent(const void* sdlEvent) = 0;
};

// Create the presentation backend for `api`. Phase 1 implements OpenGL only; any other
// value logs a warning and falls back to OpenGL.
std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(GraphicsApi api);
