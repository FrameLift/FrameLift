#include "GlGraphicsBackend.h"

#include "GlVideoRenderer.h"

#include <QtGui/QOpenGLContext>

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

void GlGraphicsBackend::ConfigureQtWindow(QQuickWindow* /*window*/)
{
}

void GlGraphicsBackend::OnQtWindowCreated(QQuickWindow* /*window*/)
{
    // Adopt Qt's scene-graph GL context (current on this thread when the first
    // VideoRenderNode::render() runs). Qt creates, owns, makes-current, and presents it;
    // the backend only resolves entry points through it and draws into the bound target.
    context_ = QOpenGLContext::currentContext();

    if (const auto* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR)))
    {
        nvidiaAdapter_ = std::strstr(vendor, "NVIDIA") != nullptr || std::strstr(vendor, "Nvidia") != nullptr ||
                         std::strstr(vendor, "nvidia") != nullptr;
    }
}

void GlGraphicsBackend::PrepareQtFrame(QQuickWindow* /*window*/)
{
}

void GlGraphicsBackend::Shutdown()
{
    // Qt owns the context; nothing to destroy here. The video renderer's GL resources are
    // released by the renderer itself (the player drops it before this is called).
    context_ = nullptr;
}

std::unique_ptr<IVideoRenderer> GlGraphicsBackend::CreateVideoRenderer()
{
    return std::make_unique<GlVideoRenderer>();
}

uintptr_t GlGraphicsBackend::CreateUITexture(const unsigned char* rgba, int w, int h)
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
    if (!context_)
    {
        return nullptr;
    }
    return reinterpret_cast<void*>(context_->getProcAddress(name));
}
