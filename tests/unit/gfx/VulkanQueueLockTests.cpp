#include "VulkanQueueLock.h"

#include <gtest/gtest.h>

#include <thread>

namespace
{
bool TryLockFromAnotherThread(VulkanQueueLock& lock, uint32_t family, uint32_t index)
{
    bool acquired = false;
    std::thread t([&] {
        acquired = lock.TryLock(family, index);
        if (acquired)
        {
            lock.Unlock(family, index);
        }
    });
    t.join();
    return acquired;
}
} // namespace

TEST(VulkanQueueLockTests, SameFamilyDifferentQueueIndicesDoNotBlockEachOther)
{
    VulkanQueueLock lock;

    lock.Lock(3, 0);
    EXPECT_FALSE(TryLockFromAnotherThread(lock, 3, 0));
    const bool acquired = lock.TryLock(3, 1);
    EXPECT_TRUE(acquired);

    if (acquired)
    {
        lock.Unlock(3, 1);
    }
    lock.Unlock(3, 0);
}

TEST(VulkanQueueLockTests, SameQueueIndexInDifferentFamiliesDoesNotBlock)
{
    VulkanQueueLock lock;

    lock.Lock(3, 0);
    const bool acquired = lock.TryLock(4, 0);
    EXPECT_TRUE(acquired);

    if (acquired)
    {
        lock.Unlock(4, 0);
    }
    lock.Unlock(3, 0);
}

TEST(VulkanQueueLockTests, OutOfRangeValuesShareOverflowSlot)
{
    VulkanQueueLock lock;

    lock.Lock(VulkanQueueLock::kMaxFamilies + 10, VulkanQueueLock::kMaxQueuesPerFamily + 10);
    EXPECT_FALSE(TryLockFromAnotherThread(lock, VulkanQueueLock::kMaxFamilies - 1,
                                          VulkanQueueLock::kMaxQueuesPerFamily - 1));

    lock.Unlock(VulkanQueueLock::kMaxFamilies + 10, VulkanQueueLock::kMaxQueuesPerFamily + 10);
}
