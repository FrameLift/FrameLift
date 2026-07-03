#pragma once

#include "IGraphicsBackend.h"

class QOpenGLContext;

// OpenGL 3.3-core presentation backend. Under Qt it does NOT own a GL context: Qt's
// scene graph creates and owns the context, and this backend ADOPTS it (the currently
// current QOpenGLContext) in OnQtWindowCreated(), resolving its GL entry points through
// it. The actual video draw happens inside VideoRenderNode::render() via the paired
// GlVideoRenderer. May #include <QtGui/...> and system GL headers (same allowance as
// QtAppWindow). All methods run on the host's GUI / scene-graph render thread.
class GlGraphicsBackend final : public IGraphicsBackend
{
public:
    void ConfigureQtWindow(QQuickWindow* window) override;
    void OnQtWindowCreated(QQuickWindow* window) override;
    void PrepareQtFrame(QQuickWindow* window) override;
    void Shutdown() override;

    [[nodiscard]] const char* Name() const override
    {
        return "OpenGL";
    }

    [[nodiscard]] bool HasNvidiaAdapter() const noexcept override
    {
        return nvidiaAdapter_;
    }

    [[nodiscard]] std::unique_ptr<IVideoRenderer> CreateVideoRenderer() override;

    [[nodiscard]] void* GetProcAddr(const char* name) const override;

private:
    QOpenGLContext* context_ = nullptr; // Qt-owned; adopted, not created here
    bool nvidiaAdapter_ = false;
};
