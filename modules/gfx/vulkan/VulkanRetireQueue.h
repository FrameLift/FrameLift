#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

// Deferred destruction for GPU objects that may still be referenced by frames in
// flight, replacing mid-frame vkDeviceWaitIdle stalls. Vulkan-free on purpose (the
// VulkanQueueLock.h precedent) so the retirement policy is unit-testable.
//
// Safety argument: Qt Quick reuses a frame slot only after waiting that slot's fence,
// and every extra submit FrameLift makes lands on the same queue *earlier in
// submission order* than Qt's own frame submit — the fence therefore also covers our
// work. BeginFrame() is ticked once per prepared scene-graph frame; an object retired
// during frame N is thus guaranteed GPU-idle once `framesInFlight` further frames have
// begun. Collection uses one extra frame of margin: skipped ticks (frames where the
// video node isn't prepared) only delay destruction, never accelerate it.
//
// Single-threaded by design: all calls happen on Qt's render thread.
class VulkanRetireQueue
{
public:
    // Queue an object's destructor; runs once the retire frame is provably GPU-idle
    // (or at Drain()). The callable owns the handles it needs.
    void Retire(std::function<void()> destroy)
    {
        if (destroy)
        {
            items_.push_back({frame_, std::move(destroy)});
        }
    }

    // Advance the frame counter (call once per prepared frame, before recording) and
    // destroy every item retired more than `framesInFlight` frames ago.
    void BeginFrame(uint32_t framesInFlight)
    {
        ++frame_;
        Collect(framesInFlight);
    }

    // Destroy items older than framesInFlight (+1 margin) without advancing the frame.
    void Collect(uint32_t framesInFlight)
    {
        std::size_t keep = 0;
        for (std::size_t i = 0; i < items_.size(); ++i)
        {
            if (frame_ - items_[i].retiredAt > framesInFlight)
            {
                items_[i].destroy();
            }
            else
            {
                if (keep != i)
                {
                    items_[keep] = std::move(items_[i]);
                }
                ++keep;
            }
        }
        items_.resize(keep);
    }

    // Destroy everything immediately. Only valid when the GPU is known idle
    // (vkDeviceWaitIdle'd shutdown/teardown paths).
    void Drain()
    {
        for (Item& item : items_)
        {
            item.destroy();
        }
        items_.clear();
    }

    [[nodiscard]] std::size_t Pending() const
    {
        return items_.size();
    }

    [[nodiscard]] uint64_t CurrentFrame() const
    {
        return frame_;
    }

private:
    struct Item
    {
        uint64_t retiredAt = 0;
        std::function<void()> destroy;
    };

    uint64_t frame_ = 0;
    std::vector<Item> items_;
};
