#include "VulkanTextureRing.h"

#include "QtTestRunner.h"

#include <QtTest/QtTest>

class VulkanTextureRingTests final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void SteadyStateUploadEveryFrameNeverStarves()
    {
        // framesInFlight + 2 slots is the documented steady-state minimum.
        constexpr uint32_t kFif = 3;
        VulkanTextureRing ring;
        ring.Reset(kFif + 2);

        for (uint64_t frame = 1; frame <= 1000; ++frame)
        {
            const int slot = ring.AcquireWritable(frame, kFif);
            QVERIFY(slot >= 0);
            QVERIFY(slot != ring.Displayed());
            ring.MarkWritten(slot);
            ring.MarkDisplayedSampled(frame);
        }
    }

    void DisplayedSlotIsNeverHandedOut()
    {
        VulkanTextureRing ring;
        ring.Reset(3);
        const int first = ring.AcquireWritable(1, 1);
        ring.MarkWritten(first);

        // Paused playback: the displayed slot is re-sampled every frame and must never
        // be offered for writing, no matter how much time passes.
        for (uint64_t frame = 2; frame < 50; ++frame)
        {
            ring.MarkDisplayedSampled(frame);
            const int slot = ring.AcquireWritable(frame, 1);
            QVERIFY(slot != first);
        }
    }

    void RecentlySampledSlotsAreWithheld()
    {
        VulkanTextureRing ring;
        ring.Reset(2);
        const int a = ring.AcquireWritable(1, 2);
        ring.MarkWritten(a);
        ring.MarkDisplayedSampled(1);

        const int b = ring.AcquireWritable(2, 2);
        QVERIFY(b >= 0);
        ring.MarkWritten(b);
        ring.MarkDisplayedSampled(2);

        // Slot a was sampled at frame 1; with framesInFlight=2 it stays withheld until
        // currentFrame - 1 > 2, i.e. frame 4.
        QCOMPARE(ring.AcquireWritable(3, 2), -1);
        QCOMPARE(ring.AcquireWritable(4, 2), a);
    }

    void PrefersLeastRecentlySampledSlot()
    {
        VulkanTextureRing ring;
        ring.Reset(4);
        // Sample slots 0..2 at increasing frames by writing + sampling each.
        for (int s = 0; s < 3; ++s)
        {
            ring.MarkWritten(s);
            ring.MarkDisplayedSampled(static_cast<uint64_t>(s + 1));
        }
        ring.MarkWritten(3); // slot 3 displayed, never sampled

        // Slots 0,1,2 idle since frames 1,2,3; far in the future all qualify — the
        // least recently sampled (slot 0) must win.
        QCOMPARE(ring.AcquireWritable(100, 2), 0);
    }

    void ResetForgetsHistory()
    {
        VulkanTextureRing ring;
        ring.Reset(2);
        ring.MarkWritten(0);
        ring.MarkDisplayedSampled(5);
        ring.Reset(2);
        QCOMPARE(ring.Displayed(), -1);
        QCOMPARE(ring.AcquireWritable(1, 4), 0); // never-sampled again after reset
    }
};

namespace
{
const ::framelift::test::Registrar<VulkanTextureRingTests> kRegisterVulkanTextureRingTests{"VulkanTextureRingTests"};
}

#include "VulkanTextureRingTests.moc"
