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
    if (!prepareCb_ || !window_ || !window_->rendererInterface() ||
        window_->rendererInterface()->graphicsApi() != QSGRendererInterface::Vulkan)
    {
        return;
    }
    const FbRect fb = FramebufferRect();
    if (fb.w > 0 && fb.h > 0)
    {
        prepareCb_(fb.x, fb.y, fb.w, fb.h);
    }
}

void VideoRenderNode::render(const RenderState* /*state*/)
{
    if (!renderCb_ || !window_)
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
    if (window_->rendererInterface() &&
        window_->rendererInterface()->graphicsApi() != QSGRendererInterface::Vulkan && prepareCb_)
    {
        prepareCb_(fb.x, fb.y, fb.w, fb.h);
    }
    renderCb_(fb.x, fb.y, fb.w, fb.h);
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
