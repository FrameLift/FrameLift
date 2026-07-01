#pragma once

#include <QtQuick/QSGRenderNode>

#include <functional>

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

    // Host video draw: given the target framebuffer size in pixels, clears to black and
    // draws the current frame letterboxed. Set by VideoItem (forwarded from QtAppWindow,
    // ultimately App's player_->RenderFrame).
    using RenderCallback = std::function<void(int fbW, int fbH)>;

    // Per-update state pushed from VideoItem::updatePaintNode().
    void SetPrepareCallback(RenderCallback cb)
    {
        prepareCb_ = std::move(cb);
    }

    void SetRenderCallback(RenderCallback cb)
    {
        renderCb_ = std::move(cb);
    }

    void SetWindow(QQuickWindow* window)
    {
        window_ = window;
    }

    void SetItemSize(int logicalW, int logicalH)
    {
        itemW_ = logicalW;
        itemH_ = logicalH;
    }

    void prepare() override;
    void render(const RenderState* state) override;
    StateFlags changedStates() const override;
    RenderingFlags flags() const override;

private:
    RenderCallback prepareCb_;
    RenderCallback renderCb_;
    QQuickWindow* window_ = nullptr;
    int itemW_ = 0;
    int itemH_ = 0;

    [[nodiscard]] std::pair<int, int> FramebufferSize() const;
};
