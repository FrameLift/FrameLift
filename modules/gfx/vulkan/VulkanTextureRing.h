#pragma once

#include <cstdint>

// Slot policy for a small ring of sampled images written by the CPU via host image
// copy. Host copies have no implicit synchronization with in-flight GPU reads, so a
// slot may only be rewritten once no frame that sampled it can still be executing.
// Vulkan-free on purpose (the VulkanQueueLock.h precedent) so the policy is
// unit-testable; the renderer owns the actual images and indexes them by slot.
//
// Frame numbers come from the backend's per-rendered-frame counter. A slot sampled in
// frame N is provably GPU-idle once `framesInFlight` further frames have been ticked
// (Qt waits the slot fence — see VulkanRetireQueue.h for the full argument); the
// strict `>` comparison adds one frame of margin. The displayed slot is never handed
// out: under paused playback it is re-sampled every frame.
class VulkanTextureRing
{
public:
    static constexpr uint32_t kMaxSlots = 8;

    void Reset(uint32_t count)
    {
        count_ = count > kMaxSlots ? kMaxSlots : count;
        displayed_ = -1;
        for (uint32_t i = 0; i < kMaxSlots; ++i)
        {
            lastSampled_[i] = 0;
            everSampled_[i] = false;
        }
    }

    // Slot safe to overwrite now, or -1 when every candidate may still be read by an
    // in-flight frame (steady state never starves when count >= framesInFlight + 2).
    // Prefers never-sampled slots, then the least recently sampled one.
    [[nodiscard]] int AcquireWritable(uint64_t currentFrame, uint32_t framesInFlight) const
    {
        int best = -1;
        uint64_t bestSampled = ~uint64_t{0};
        for (uint32_t i = 0; i < count_; ++i)
        {
            if (static_cast<int>(i) == displayed_)
            {
                continue;
            }
            if (!everSampled_[i])
            {
                return static_cast<int>(i);
            }
            if (currentFrame - lastSampled_[i] > framesInFlight && lastSampled_[i] < bestSampled)
            {
                best = static_cast<int>(i);
                bestSampled = lastSampled_[i];
            }
        }
        return best;
    }

    // The freshly written slot becomes the displayed one.
    void MarkWritten(int slot)
    {
        if (slot >= 0 && static_cast<uint32_t>(slot) < count_)
        {
            displayed_ = slot;
        }
    }

    // Call whenever the displayed slot's image is sampled by a frame.
    void MarkDisplayedSampled(uint64_t frame)
    {
        if (displayed_ >= 0)
        {
            lastSampled_[displayed_] = frame;
            everSampled_[displayed_] = true;
        }
    }

    [[nodiscard]] int Displayed() const
    {
        return displayed_;
    }

    [[nodiscard]] uint32_t Count() const
    {
        return count_;
    }

private:
    uint32_t count_ = 0;
    int displayed_ = -1;
    uint64_t lastSampled_[kMaxSlots]{};
    bool everSampled_[kMaxSlots]{};
};
