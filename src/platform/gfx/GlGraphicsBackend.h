#pragma once

#include "IGraphicsBackend.h"

struct SDL_Window;

// OpenGL 3.3-core presentation backend: owns the SDL GL context and drives the
// imgui_impl_opengl3 + imgui_impl_sdl3 backends. May #include <SDL3/SDL.h> and
// imgui_impl_*.h (same allowance as SdlAppWindow). All methods run on the host's
// main / render thread.
class GlGraphicsBackend final : public IGraphicsBackend
{
public:
    uint64_t PreWindowCreate() override;
    void OnWindowCreated(SDL_Window* window) override;
    void Shutdown() override;
    [[nodiscard]] const char* Name() const override { return "OpenGL"; }
    [[nodiscard]] bool HasNvidiaAdapter() const noexcept override { return nvidiaAdapter_; }

    [[nodiscard]] std::unique_ptr<IVideoRenderer> CreateVideoRenderer() override;
    [[nodiscard]] uintptr_t CreateUiTexture(const unsigned char* rgba, int w, int h) override;

    [[nodiscard]] void* GetProcAddr(const char* name) const override;
    bool BeginFrame() override;
    void SwapBuffers() override;
    void SetVSync(bool enabled) override;

    void ImGuiInitBackends() override;
    void ImGuiShutdownBackends() override;
    void ImGuiNewFrame() override;
    void ImGuiRenderDrawData() override;
    void ImGuiRenderPlatformWindows() override;
    void ImGuiProcessEvent(const void* sdlEvent) override;

private:
    SDL_Window* window_ = nullptr;
    void* glContext_ = nullptr; // SDL_GLContext (kept as void* to keep SDL out of this header)
    bool shown_ = false;        // window is created hidden, then shown on the first SwapBuffers()
    bool nvidiaAdapter_ = false;

    // Resolved in OnWindowCreated; used by BeginFrame to clear the default framebuffer
    // to black each frame (covers the no-renderer fallback and the letterbox bars).
    void (*glClearColor_)(float, float, float, float) = nullptr;
    void (*glClear_)(unsigned int) = nullptr;
};
