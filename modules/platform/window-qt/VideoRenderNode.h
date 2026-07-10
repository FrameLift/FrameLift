#pragma once

#include <QtQuick/QSGRenderNode>

#include "VideoRenderCallbacks.h"

#include <memory>

class QQuickWindow;

// Scene-graph node that draws the decoded video frame inside the QQuickWindow render
// pass. With the OpenGL RHI forced (QSGRendererInterface::OpenGL), render() runs while
// Qt's GL context is current; it drives the host's video draw (FFmpegPlayer::RenderFrame
// via the graphics backend's GlVideoRenderer) through a callback, bracketed by the scene
// graph's external-commands convention so Qt's RHI state tracking is not corrupted.
class VideoRenderNode final : public QSGRenderNode
{
public:
    VideoRenderNode() = default;

    // Per-update state pushed from VideoItem::updatePaintNode(). The shared binding
    // is deliberately invalidatable so old nodes cannot call into a torn-down App.
    void SetCallbacks(std::shared_ptr<VideoRenderCallbacks> callbacks)
    {
        callbacks_ = std::move(callbacks);
    }

    void SetWindow(QQuickWindow* window)
    {
        window_ = window;
    }

    void SetItemRect(int logicalX, int logicalY, int logicalW, int logicalH)
    {
        itemX_ = logicalX;
        itemY_ = logicalY;
        itemW_ = logicalW;
        itemH_ = logicalH;
    }

    void prepare() override;
    void render(const RenderState* state) override;
    StateFlags changedStates() const override;
    RenderingFlags flags() const override;

private:
    std::shared_ptr<VideoRenderCallbacks> callbacks_;
    QQuickWindow* window_ = nullptr;
    int itemX_ = 0;
    int itemY_ = 0;
    int itemW_ = 0;
    int itemH_ = 0;

    struct FbRect
    {
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
    };

    [[nodiscard]] FbRect FramebufferRect() const;
};
