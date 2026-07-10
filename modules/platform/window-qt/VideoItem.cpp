#include "VideoItem.h"
#include "VideoRenderNode.h"

#include <QtQuick/QQuickWindow>

VideoItem::VideoItem(QQuickItem* parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

void VideoItem::SetRenderCallbacks(
    std::function<void(int, int, int, int)> prepareCb, std::function<void(int, int, int, int)> renderCb
)
{
    callbacks_->Set(std::move(prepareCb), std::move(renderCb));
    update();
}

QSGNode* VideoItem::updatePaintNode(QSGNode* old, UpdatePaintNodeData* /*data*/)
{
    auto* node = static_cast<VideoRenderNode*>(old);
    if (!node)
    {
        node = new VideoRenderNode();
    }
    node->SetWindow(window());
    // Direct child of the window content item, so position() is the scene position;
    // y is the chrome inset when the fallback title bar reserves the top strip.
    node->SetItemRect(
        static_cast<int>(x()), static_cast<int>(y()), static_cast<int>(width()), static_cast<int>(height())
    );
    node->SetCallbacks(callbacks_);
    return node;
}
