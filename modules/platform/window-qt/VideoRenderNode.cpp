#include "VideoRenderNode.h"

#include <QtQuick/QQuickWindow>
#include <QtQuick/QSGRendererInterface>

VideoRenderNode::FbRect VideoRenderNode::FramebufferRect() const
{
    if (!window_)
    {
        return {};
    }
    const qreal dpr = window_->effectiveDevicePixelRatio();
    return {
        static_cast<int>(itemX_ * dpr), static_cast<int>(itemY_ * dpr), static_cast<int>(itemW_ * dpr),
        static_cast<int>(itemH_ * dpr)
    };
}

void VideoRenderNode::prepare()
{
    if (!callbacks_ || !callbacks_->HasPrepare() || !window_ || !window_->rendererInterface() ||
        window_->rendererInterface()->graphicsApi() != QSGRendererInterface::Vulkan)
    {
        return;
    }
    const FbRect fb = FramebufferRect();
    if (fb.w > 0 && fb.h > 0)
    {
        // Vulkan uploads and decode-image acquire barriers are recorded into Qt's
        // current command buffer here, before the scene graph begins its render pass.
        // beginExternalCommands() may replace the native command-buffer handle, so the
        // callback refreshes it only after this call.
        window_->beginExternalCommands();
        callbacks_->Prepare(fb.x, fb.y, fb.w, fb.h);
        window_->endExternalCommands();
    }
}

void VideoRenderNode::render(const RenderState* /*state*/)
{
    if (!callbacks_ || !callbacks_->HasRender() || !window_)
    {
        return;
    }

    // The target framebuffer rect is the item's logical geometry scaled by the device
    // pixel ratio; the origin is nonzero when the fallback title bar insets the item.
    // The host video renderer letterboxes within this rect.
    const FbRect fb = FramebufferRect();
    if (fb.w <= 0 || fb.h <= 0)
    {
        return;
    }

    // Tell Qt's RHI we are issuing native API commands outside its tracking, so it can
    // flush pending state and restore afterwards.
    window_->beginExternalCommands();
    if (window_->rendererInterface() && window_->rendererInterface()->graphicsApi() != QSGRendererInterface::Vulkan &&
        callbacks_->HasPrepare())
    {
        callbacks_->Prepare(fb.x, fb.y, fb.w, fb.h);
    }
    callbacks_->Render(fb.x, fb.y, fb.w, fb.h);
    window_->endExternalCommands();
}

QSGRenderNode::StateFlags VideoRenderNode::changedStates() const
{
    // The raw-GL video draw touches these; report them so the SG restores its own state.
    return {DepthState | StencilState | ScissorState | ColorState | BlendState | CullState | ViewportState};
}

QSGRenderNode::RenderingFlags VideoRenderNode::flags() const
{
    // Video fills the item opaquely; it draws in the item's coordinate space.
    return {BoundedRectRendering | OpaqueRendering};
}
