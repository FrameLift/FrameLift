#pragma once

#include <array>
#include <cstdint>
#include <mutex>

// Serializes access to Vulkan queues shared by Qt Quick's render thread,
// FrameLift's renderer, and FFmpeg's decode thread. A VkQueue is not
// thread-safe: every vkQueueSubmit / vkQueuePresentKHR / vkQueueWaitIdle on a
// given queue must be externally synchronized.
//
// Deliberately Vulkan-type-free so it can be included from both the backend and the
// FFmpeg hwaccel bridge. The lock is keyed by queue family and queue index because
// FFmpeg's lock_queue(ctx, family, index) callback supplies both, and the Vulkan
// backend may reserve graphics-family queue index 1 for Qt Quick.
class VulkanQueueLock
{
public:
    // Queue family and index counts are small. Out-of-range values fold onto the
    // last slot, which over-serializes instead of ever missing a lock.
    static constexpr uint32_t kMaxFamilies = 32;
    static constexpr uint32_t kMaxQueuesPerFamily = 8;

    // Turn locking on/off. When the device's queues are created internally synchronized
    // (VK_KHR_internally_synchronized_queues), the driver makes concurrent submits safe,
    // so the renderer's guards and FFmpeg's lock_queue callback are both unnecessary and
    // this lock becomes a no-op. Set once at device-creation time, before any threads run.
    void SetEnabled(bool enabled) noexcept { enabled_ = enabled; }

    void Lock(uint32_t family, uint32_t index) noexcept
    {
        if (enabled_)
        {
            mutexes_[FamilySlot(family)][QueueSlot(index)].lock();
        }
    }

    bool TryLock(uint32_t family, uint32_t index) noexcept
    {
        return enabled_ ? mutexes_[FamilySlot(family)][QueueSlot(index)].try_lock() : true;
    }

    void Unlock(uint32_t family, uint32_t index) noexcept
    {
        if (enabled_)
        {
            mutexes_[FamilySlot(family)][QueueSlot(index)].unlock();
        }
    }

private:
    static uint32_t FamilySlot(uint32_t family) noexcept { return family < kMaxFamilies ? family : kMaxFamilies - 1; }

    static uint32_t QueueSlot(uint32_t index) noexcept
    {
        return index < kMaxQueuesPerFamily ? index : kMaxQueuesPerFamily - 1;
    }

    bool enabled_ = true;
    std::array<std::array<std::mutex, kMaxQueuesPerFamily>, kMaxFamilies> mutexes_{};
};

// Scoped lock over one queue. Hold this around the render thread's queue
// operations so they cannot interleave with FFmpeg's decode-thread submissions
// on that same queue.
class VulkanQueueGuard
{
public:
    VulkanQueueGuard(VulkanQueueLock& lock, uint32_t family, uint32_t index = 0) noexcept
        : lock_(lock), family_(family), index_(index)
    {
        lock_.Lock(family_, index_);
    }

    ~VulkanQueueGuard() { lock_.Unlock(family_, index_); }

    VulkanQueueGuard(const VulkanQueueGuard&) = delete;
    VulkanQueueGuard& operator=(const VulkanQueueGuard&) = delete;

private:
    VulkanQueueLock& lock_;
    uint32_t family_;
    uint32_t index_;
};
