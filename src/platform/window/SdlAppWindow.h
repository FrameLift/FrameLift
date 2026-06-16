#pragma once

#include <SDL3/SDL.h>
#include <framelift/platform/IAppWindow.h>
#include <memory>
#include <string>

#include "../gfx/IGraphicsBackend.h"

// Concrete IAppWindow backed by SDL3 + Dear ImGui. Owns the SDL window and event
// loop and delegates the rendering surface (GL/Vulkan context, present, vsync, and
// the imgui_impl_* lifecycle) to an IGraphicsBackend.
//
// This file and the gfx backends (e.g. GlGraphicsBackend) are the only files that
// may #include <SDL3/SDL.h> or imgui_impl_*.h.
class SdlAppWindow final : public IAppWindow
{
public:
    SdlAppWindow(const char* title, int width, int height);
    ~SdlAppWindow() override;

    void GetSize(int& w, int& h) const noexcept override;
    void SetSize(int w, int h) noexcept override;
    [[nodiscard]] bool IsFullscreen() const noexcept override;
    void SetFullscreen(bool fs) noexcept override;
    [[nodiscard]] Rect GetDisplayUsableBounds() const noexcept override;
    [[nodiscard]] void* GetNativeHandle() const noexcept override;
    bool SetWindowIcon(const char* path) noexcept override;
    bool SetWindowIconFromMemory(const unsigned char* data, int size) noexcept override;
    void SetTitle(const char* title) noexcept override;

    [[nodiscard]] void* GetGLProcAddr(const char* name) const noexcept override;
    void SwapBuffers() noexcept override;
    void SetVSync(bool enabled) noexcept override;
    [[nodiscard]] bool IsRenderable() const noexcept override;

    bool WaitNextEvent(AppEvent& out, int timeoutMs) noexcept override;
    bool PollNextEvent(AppEvent& out) noexcept override;
    [[nodiscard]] uint32_t RegisterCustomEventType() noexcept override;
    void PushCustomEvent(uint32_t eventType, void* data1) noexcept override;
    void PushRenderUpdate() noexcept override;
    void PushPlayerWakeup() noexcept override;
    void PushQuitEvent() noexcept override;

    int GetPrefPath(const char* org, const char* app, char* buf, int cap) const noexcept override;
    int GetBasePath(char* buf, int cap) const noexcept override;

    void SetImGuiIniPath(const char* path) noexcept override;
    void ImGuiInit() noexcept override;
    void ImGuiShutdown() noexcept override;
    void UIBeginFrame() noexcept override;
    void UIEndFrame() noexcept override;
    void ImGuiProcessEvent(const AppEvent& event) noexcept override;

private:
    [[nodiscard]] AppEvent TranslateEvent(SDL_Event e) const;
    [[nodiscard]] static Key TranslateKey(SDL_Keycode k);
    [[nodiscard]] static Mod TranslateMods(SDL_Keymod m);

    SDL_Window* window_ = nullptr;
    std::unique_ptr<IGraphicsBackend> backend_;
    std::string iniPath_;
    uint32_t renderUpdateEventType_ = 0;
    uint32_t playerWakeupEventType_ = 0;
};