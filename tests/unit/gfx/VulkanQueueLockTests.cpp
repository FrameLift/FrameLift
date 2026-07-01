#include "VulkanQueueLock.h"

#include "QtTestRunner.h"

#include <QtTest/QtTest>

#include <thread>

namespace
{
bool TryLockFromAnotherThread(VulkanQueueLock& lock, uint32_t family, uint32_t index)
{
    bool acquired = false;
    std::thread t(
        [&]
        {
            acquired = lock.TryLock(family, index);
            if (acquired)
            {
                lock.Unlock(family, index);
            }
        }
    );
    t.join();
    return acquired;
}
} // namespace

class VulkanQueueLockTests final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void SameFamilyDifferentQueueIndicesDoNotBlockEachOther()
    {
        VulkanQueueLock lock;

        lock.Lock(3, 0);
        QVERIFY(!(TryLockFromAnotherThread(lock, 3, 0)));
        const bool acquired = lock.TryLock(3, 1);
        QVERIFY(acquired);

        if (acquired)
        {
            lock.Unlock(3, 1);
        }
        lock.Unlock(3, 0);
    }

    void SameQueueIndexInDifferentFamiliesDoesNotBlock()
    {
        VulkanQueueLock lock;

        lock.Lock(3, 0);
        const bool acquired = lock.TryLock(4, 0);
        QVERIFY(acquired);

        if (acquired)
        {
            lock.Unlock(4, 0);
        }
        lock.Unlock(3, 0);
    }

    void OutOfRangeValuesShareOverflowSlot()
    {
        VulkanQueueLock lock;

        lock.Lock(VulkanQueueLock::kMaxFamilies + 10, VulkanQueueLock::kMaxQueuesPerFamily + 10);
        QVERIFY(!(
            TryLockFromAnotherThread(lock, VulkanQueueLock::kMaxFamilies - 1, VulkanQueueLock::kMaxQueuesPerFamily - 1)
        ));

        lock.Unlock(VulkanQueueLock::kMaxFamilies + 10, VulkanQueueLock::kMaxQueuesPerFamily + 10);
    }
};

namespace
{
const ::framelift::test::Registrar<VulkanQueueLockTests> kRegisterVulkanQueueLockTests{"VulkanQueueLockTests"};
}

#include "VulkanQueueLockTests.moc"
