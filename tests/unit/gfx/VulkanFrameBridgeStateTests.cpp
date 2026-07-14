#include "VulkanFrameBridgeState.h"

#include "QtTestRunner.h"

#include <QtTest/QtTest>

class VulkanFrameBridgeStateTests final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void PrefersNonQtGraphicsQueue()
    {
        const auto queue = VulkanFrameBridge::SelectQueue(3, 1, 7, true);
        QCOMPARE(queue.family, 3);
        QCOMPARE(queue.index, uint32_t{0});
    }

    void UsesDistinctVideoQueueWhenGraphicsHasOnlyQtQueue()
    {
        const auto queue = VulkanFrameBridge::SelectQueue(3, 0, 7, true);
        QCOMPARE(queue.family, 7);
        QCOMPARE(queue.index, uint32_t{0});
    }

    void RejectsQueueSharedWithQt()
    {
        QVERIFY(!VulkanFrameBridge::SelectQueue(3, 0, 3, true).Valid());
        QVERIFY(!VulkanFrameBridge::SelectQueue(3, 0, -1, false).Valid());
    }

    void ExposesOnlyGraphicsQueueIndexIsolatedFromQt()
    {
        QVERIFY(!VulkanFrameBridge::GraphicsQueueIsIsolatedFromQt(0));
        QVERIFY(VulkanFrameBridge::GraphicsQueueIsIsolatedFromQt(1));
    }

    void SlotReuseActionsPreserveSubmissionBoundary()
    {
        using enum VulkanFrameBridge::Phase;
        using enum VulkanFrameBridge::ReuseAction;
        QCOMPARE(VulkanFrameBridge::ActionForReuse(Available), None);
        QCOMPARE(VulkanFrameBridge::ActionForReuse(Collecting), UnlockUnsubmitted);
        QCOMPARE(VulkanFrameBridge::ActionForReuse(QtSubmissionInstalled), HostSignalCompletedQtWork);
        QCOMPARE(VulkanFrameBridge::ActionForReuse(CompletionInFlight), WaitForCompletion);
        QCOMPARE(VulkanFrameBridge::ActionForReuse(HostFallbackPending), HostSignalCompletedQtWork);
    }

    void QtSubmissionPairCanBeInstalledExactlyOnce()
    {
        auto phase = VulkanFrameBridge::Phase::Collecting;
        QVERIFY(VulkanFrameBridge::TryMarkQtSubmissionInstalled(phase));
        QCOMPARE(phase, VulkanFrameBridge::Phase::QtSubmissionInstalled);
        QVERIFY(!VulkanFrameBridge::TryMarkQtSubmissionInstalled(phase));
    }
};

namespace
{
const ::framelift::test::Registrar<VulkanFrameBridgeStateTests> kRegisterVulkanFrameBridgeStateTests{
    "VulkanFrameBridgeStateTests"
};
}

#include "VulkanFrameBridgeStateTests.moc"
