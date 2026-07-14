#include "VulkanTimelineSignalState.h"

#include "QtTestRunner.h"

#include <QtTest/QtTest>

#include <limits>

class VulkanTimelineSignalStateTests final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void ReservationDoesNotPublishBeforeSubmission()
    {
        VulkanTimelineSignalState state;
        state.BeginPool(10);

        const auto reservation = state.Reserve(20, 41);
        QVERIFY(reservation.has_value());
        QCOMPARE(reservation->value, uint64_t{41});
        QCOMPARE(state.DeliveredValue(20), uint64_t{0});

        QVERIFY(state.MarkDelivered(20, reservation->value));
        QCOMPARE(state.DeliveredValue(20), uint64_t{41});
    }

    void RepeatedAndLowerRequestsAdvanceMonotonically()
    {
        VulkanTimelineSignalState state;
        state.BeginPool(1);

        const auto first = state.Reserve(5, 12);
        const auto repeated = state.Reserve(5, 12);
        const auto lower = state.Reserve(5, 7);
        QVERIFY(first && repeated && lower);
        QCOMPARE(first->value, uint64_t{12});
        QCOMPARE(repeated->value, uint64_t{13});
        QVERIFY(repeated->adjusted);
        QCOMPARE(lower->value, uint64_t{14});
        QVERIFY(lower->adjusted);
    }

    void HigherFfmpegBaseIsAccepted()
    {
        VulkanTimelineSignalState state;
        state.BeginPool(1);
        QVERIFY(state.Reserve(5, 3).has_value());

        const auto reservation = state.Reserve(5, 99);
        QVERIFY(reservation.has_value());
        QCOMPARE(reservation->value, uint64_t{99});
        QVERIFY(!reservation->adjusted);
    }

    void CoalescedPendingReservationsPublishTheHighestValue()
    {
        VulkanTimelineSignalState state;
        state.BeginPool(1);

        const auto first = state.Reserve(5, 12);
        const auto coalesced = state.Reserve(5, 12);
        QVERIFY(first && coalesced);
        QCOMPARE(coalesced->value, uint64_t{13});
        QCOMPARE(state.DeliveredValue(5), uint64_t{0});
        QVERIFY(state.MarkDelivered(5, coalesced->value));
        QCOMPARE(state.DeliveredValue(5), uint64_t{13});
    }

    void SemaphoresAdvanceIndependently()
    {
        VulkanTimelineSignalState state;
        state.BeginPool(1);
        QVERIFY(state.Reserve(5, 8).has_value());
        QVERIFY(state.Reserve(5, 8).has_value());

        const auto other = state.Reserve(6, 8);
        QVERIFY(other.has_value());
        QCOMPARE(other->value, uint64_t{8});
    }

    void PoolChangeForgetsOldHandleHistory()
    {
        VulkanTimelineSignalState state;
        state.BeginPool(1);
        QVERIFY(state.Reserve(5, 100).has_value());
        QVERIFY(state.MarkDelivered(5, 100));

        state.BeginPool(2);
        QCOMPARE(state.DeliveredValue(5), uint64_t{0});
        const auto reusedHandle = state.Reserve(5, 1);
        QVERIFY(reusedHandle.has_value());
        QCOMPARE(reusedHandle->value, uint64_t{1});
    }

    void InvalidAndExhaustedValuesAreRejected()
    {
        VulkanTimelineSignalState state;
        state.BeginPool(1);
        QVERIFY(!state.Reserve(0, 1).has_value());
        QVERIFY(!state.Reserve(5, 0).has_value());

        const uint64_t max = std::numeric_limits<uint64_t>::max();
        QVERIFY(state.Reserve(5, max).has_value());
        QVERIFY(!state.Reserve(5, max).has_value());
    }

    void DeliveryMustMatchAReservationAndAdvance()
    {
        VulkanTimelineSignalState state;
        state.BeginPool(1);
        QVERIFY(!state.MarkDelivered(5, 1));
        QVERIFY(state.Reserve(5, 4).has_value());
        QVERIFY(!state.MarkDelivered(5, 5));
        QVERIFY(state.MarkDelivered(5, 4));
        QVERIFY(!state.MarkDelivered(5, 4));
    }
};

namespace
{
const ::framelift::test::Registrar<VulkanTimelineSignalStateTests> kRegisterVulkanTimelineSignalStateTests{
    "VulkanTimelineSignalStateTests"
};
}

#include "VulkanTimelineSignalStateTests.moc"
