#include "GlGraphicsBackend.h"

#include "GlVideoRenderer.h"
#include "util.h"

#include <SDL3/SDL.h>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl3.h"

#include <cstring>

#ifdef _WIN32
// <GL/gl.h> needs the WINGDIAPI/APIENTRY macros from windows.h on MSVC/MinGW.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#include <GL/gl.h>
// GL_CLAMP_TO_EDGE is GL 1.2; the Windows <GL/gl.h> only declares 1.1.
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

uint64_t GlGraphicsBackend::PreWindowCreate()
{
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    return SDL_WINDOW_OPENGL;
}

void GlGraphicsBackend::OnWindowCreated(SDL_Window* window)
{
    window_ = window;

    SDL_GLContext ctx = SDL_GL_CreateContext(window_);
    if (!ctx)
    {
        Fatal(SDL_GetError());
    }
    glContext_ = ctx;

    SDL_GL_MakeCurrent(window_, ctx);
    SDL_GL_SetSwapInterval(0);

    glClearColor_ = reinterpret_cast<decltype(glClearColor_)>(SDL_GL_GetProcAddress("glClearColor"));
    glClear_ = reinterpret_cast<decltype(glClear_)>(SDL_GL_GetProcAddress("glClear"));

    if (const auto* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR)))
    {
        nvidiaAdapter_ = std::strstr(vendor, "NVIDIA") != nullptr || std::strstr(vendor, "Nvidia") != nullptr ||
                         std::strstr(vendor, "nvidia") != nullptr;
    }
}

void GlGraphicsBackend::Shutdown()
{
    if (glContext_)
    {
        SDL_GL_DestroyContext(static_cast<SDL_GLContext>(glContext_));
        glContext_ = nullptr;
    }
}

std::unique_ptr<IVideoRenderer> GlGraphicsBackend::CreateVideoRenderer()
{
    return std::make_unique<GlVideoRenderer>();
}

uintptr_t GlGraphicsBackend::CreateUiTexture(const unsigned char* rgba, int w, int h)
{
    if (!rgba || w <= 0 || h <= 0)
    {
        return 0;
    }
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glBindTexture(GL_TEXTURE_2D, 0);
    return static_cast<uintptr_t>(id);
}

void* GlGraphicsBackend::GetProcAddr(const char* name) const
{
    return reinterpret_cast<void*>(SDL_GL_GetProcAddress(name));
}

bool GlGraphicsBackend::BeginFrame()
{
    // The GL context is already current; clear the default framebuffer to black so
    // the frame starts clean even if the video renderer has no frame or failed init.
    // (The video renderer also clears + draws letterboxed; this guarantees black.)
    if (glClearColor_ && glClear_)
    {
        glClearColor_(0.0f, 0.0f, 0.0f, 1.0f);
        glClear_(GL_COLOR_BUFFER_BIT);
    }
    return true;
}

void GlGraphicsBackend::SwapBuffers()
{
    SDL_GL_SwapWindow(window_);

    // Reveal the window only once a complete, painted frame is in the back buffer,
    // so the user never sees the black startup framebuffer (the window is created
    // hidden — see SdlAppWindow).
    if (!shown_)
    {
        shown_ = true;
        SDL_ShowWindow(window_);
    }
}

void GlGraphicsBackend::SetVSync(bool enabled)
{
    // 1 = sync presentation to the display refresh (tear-free); 0 = present immediately.
    SDL_GL_SetSwapInterval(enabled ? 1 : 0);
}

void GlGraphicsBackend::ImGuiInitBackends()
{
    ImGui_ImplSDL3_InitForOpenGL(window_, glContext_);
    ImGui_ImplOpenGL3_Init("#version 330 core");
}

void GlGraphicsBackend::ImGuiShutdownBackends()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
}

void GlGraphicsBackend::ImGuiNewFrame()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
}

void GlGraphicsBackend::ImGuiRenderDrawData()
{
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void GlGraphicsBackend::ImGuiRenderPlatformWindows()
{
    // Render any popped-out panels into their own OS windows, then restore the
    // main window's GL context for the next frame.
    const ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        SDL_Window* backupWin = SDL_GL_GetCurrentWindow();
        SDL_GLContext backupCtx = SDL_GL_GetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        SDL_GL_MakeCurrent(backupWin, backupCtx);
    }
}

void GlGraphicsBackend::ImGuiProcessEvent(const void* sdlEvent)
{
    ImGui_ImplSDL3_ProcessEvent(static_cast<const SDL_Event*>(sdlEvent));
}
