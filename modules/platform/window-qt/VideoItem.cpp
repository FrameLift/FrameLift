#include "VideoItem.h"
#include "VideoRenderNode.h"

#include <QtQuick/QQuickWindow>

VideoItem::VideoItem(QQuickItem* parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

void VideoItem::SetRenderCallbacks(std::function<void(int, int)> prepareCb, std::function<void(int, int)> renderCb)
{
    prepareCb_ = std::move(prepareCb);
    renderCb_ = std::move(renderCb);
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
    node->SetItemSize(static_cast<int>(width()), static_cast<int>(height()));
    node->SetPrepareCallback(prepareCb_);
    node->SetRenderCallback(renderCb_);
    return node;
}
