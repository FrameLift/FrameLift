#pragma once

#include <functional>
#include <utility>

// Shared, GUI-thread-owned callback state for VideoItem and every scene-graph node
// it creates. A QSGRenderNode can survive a VideoItem callback update until the
// scene graph next synchronizes; clearing this binding therefore makes those old
// nodes harmless during App teardown.
class VideoRenderCallbacks final
{
public:
    using Callback = std::function<void(int fbX, int fbY, int fbW, int fbH)>;

    void Set(Callback prepareCb, Callback renderCb)
    {
        prepareCb_ = std::move(prepareCb);
        renderCb_ = std::move(renderCb);
    }

    void Prepare(int fbX, int fbY, int fbW, int fbH) const
    {
        if (prepareCb_)
        {
            prepareCb_(fbX, fbY, fbW, fbH);
        }
    }

    void Render(int fbX, int fbY, int fbW, int fbH) const
    {
        if (renderCb_)
        {
            renderCb_(fbX, fbY, fbW, fbH);
        }
    }

    [[nodiscard]] bool HasPrepare() const
    {
        return static_cast<bool>(prepareCb_);
    }

    [[nodiscard]] bool HasRender() const
    {
        return static_cast<bool>(renderCb_);
    }

private:
    Callback prepareCb_;
    Callback renderCb_;
};
