#pragma once

#include <QtQuick/QQuickItem>

#include <functional>

// The video surface as a real Qt Quick scene-graph citizen:
//   QQuickWindow → scene graph → VideoItem (QQuickItem) → VideoRenderNode (QSGRenderNode)
// Fills the window and, via updatePaintNode(), produces a VideoRenderNode whose render()
// draws the decoded video frame (through the host's GL video renderer) inside the
// scene-graph render pass. QML chrome can later sit on top of this item.
class VideoItem final : public QQuickItem
{
    Q_OBJECT

public:
    explicit VideoItem(QQuickItem* parent = nullptr);

    // Host video draw, forwarded to the render node (see VideoRenderNode::RenderCallback).
    // Set by QtAppWindow once the window exists; invoked on the scene-graph render thread.
    void SetRenderCallbacks(
        std::function<void(int fbX, int fbY, int fbW, int fbH)> prepareCb,
        std::function<void(int fbX, int fbY, int fbW, int fbH)> renderCb
    );

protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData* data) override;

private:
    std::function<void(int, int, int, int)> prepareCb_;
    std::function<void(int, int, int, int)> renderCb_;
};
