#include "VideoRenderNode.h"

#include <QtQuick/QQuickWindow>
#include <QtQuick/QSGRendererInterface>

std::pair<int, int> VideoRenderNode::FramebufferSize() const
{
    if (!window_)
    {
        return {0, 0};
    }
    const qreal dpr = window_->effectiveDevicePixelRatio();
    return {static_cast<int>(itemW_ * dpr), static_cast<int>(itemH_ * dpr)};
}

void VideoRenderNode::prepare()
{
    if (!prepareCb_ || !window_ || !window_->rendererInterface() ||
        window_->rendererInterface()->graphicsApi() != QSGRendererInterface::Vulkan)
    {
        return;
    }
    const auto [fbW, fbH] = FramebufferSize();
    if (fbW > 0 && fbH > 0)
    {
        prepareCb_(fbW, fbH);
    }
}

void VideoRenderNode::render(const RenderState* /*state*/)
{
    if (!renderCb_ || !window_)
    {
        return;
    }

    // The item fills the window, so the target framebuffer rect is the item's logical
    // size scaled by the device pixel ratio. The host video renderer sets its own
    // viewport and clears + letterboxes within this rect.
    const auto [fbW, fbH] = FramebufferSize();
    if (fbW <= 0 || fbH <= 0)
    {
        return;
    }

    // Tell Qt's RHI we are issuing native API commands outside its tracking, so it can
    // flush pending state and restore afterwards.
    window_->beginExternalCommands();
    if (window_->rendererInterface() &&
        window_->rendererInterface()->graphicsApi() != QSGRendererInterface::Vulkan && prepareCb_)
    {
        prepareCb_(fbW, fbH);
    }
    renderCb_(fbW, fbH);
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
