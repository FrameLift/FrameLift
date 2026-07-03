#include "VulkanRetireQueue.h"

#include "QtTestRunner.h"

#include <QtTest/QtTest>

class VulkanRetireQueueTests final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void ItemSurvivesWhileFramesMayBeInFlight()
    {
        VulkanRetireQueue q;
        int destroyed = 0;
        q.Retire(
            [&]
            {
                ++destroyed;
            }
        );

        // Retired during frame 0 with 3 frames in flight: frame 0's fence is provably
        // waited when frame 3 begins; the +1 margin delays destruction to tick 4.
        for (int frame = 1; frame <= 3; ++frame)
        {
            q.BeginFrame(3);
            QCOMPARE(destroyed, 0);
        }
        q.BeginFrame(3);
        QCOMPARE(destroyed, 1);
        QCOMPARE(q.Pending(), std::size_t{0});
    }

    void LaterItemsOutliveEarlierOnes()
    {
        VulkanRetireQueue q;
        int first = 0;
        int second = 0;
        q.Retire(
            [&]
            {
                ++first;
            }
        );
        q.BeginFrame(1); // frame 1
        q.Retire(
            [&]
            {
                ++second;
            }
        );

        q.BeginFrame(1); // frame 2: first (retired frame 0) destroyed, second (frame 1) survives
        QCOMPARE(first, 1);
        QCOMPARE(second, 0);
        q.BeginFrame(1); // frame 3: second destroyed
        QCOMPARE(second, 1);
    }

    void ZeroFramesInFlightStillDefersOneFrame()
    {
        VulkanRetireQueue q;
        int destroyed = 0;
        q.Retire(
            [&]
            {
                ++destroyed;
            }
        );
        q.Collect(0);
        QCOMPARE(destroyed, 0); // +1 margin: destruction never happens in the retire frame
        q.BeginFrame(0);
        QCOMPARE(destroyed, 1);
    }

    void DrainDestroysEverythingImmediately()
    {
        VulkanRetireQueue q;
        int destroyed = 0;
        for (int i = 0; i < 3; ++i)
        {
            q.Retire(
                [&]
                {
                    ++destroyed;
                }
            );
        }
        QCOMPARE(q.Pending(), std::size_t{3});
        q.Drain();
        QCOMPARE(destroyed, 3);
        QCOMPARE(q.Pending(), std::size_t{0});
    }

    void NullRetireIsIgnored()
    {
        VulkanRetireQueue q;
        q.Retire({});
        QCOMPARE(q.Pending(), std::size_t{0});
    }

    void CollectWithoutTickKeepsRecentItems()
    {
        VulkanRetireQueue q;
        int destroyed = 0;
        q.BeginFrame(2);
        q.BeginFrame(2);
        q.Retire(
            [&]
            {
                ++destroyed;
            }
        );
        q.Collect(2); // no tick between retire and collect
        QCOMPARE(destroyed, 0);
        QCOMPARE(q.Pending(), std::size_t{1});
    }
};

namespace
{
const ::framelift::test::Registrar<VulkanRetireQueueTests> kRegisterVulkanRetireQueueTests{"VulkanRetireQueueTests"};
}

#include "VulkanRetireQueueTests.moc"
