#include "ReadAheadCache.h"

#include "QtTestRunner.h"

#include <QtTest/QtTest>

#include <atomic>
#include <barrier>
#include <chrono>
#include <thread>
#include <vector>

// Pure-logic tests for the shared read-ahead budget + metrics. No
// FFmpeg — the queues that own AVPackets only feed bytes into this type.

class ReadAheadCacheTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void DisabledAdmitsEverything()
    {
        ReadAheadCache c;
        c.Configure(/*enabled=*/false, /*maxBytes=*/1024);
        // Byte-bounding off: Reserve never blocks regardless of used bytes.
        QVERIFY(c.Reserve(1 << 30));
        QVERIFY(c.Reserve(1 << 30));
    }

    void AdmitsUntilBudgetReached()
    {
        ReadAheadCache c;
        c.Configure(true, 1000);

        QVERIFY(c.Reserve(400));
        QVERIFY(c.Reserve(400)); // 800 <= 1000
        QVERIFY((c.UsedBytes()) == (800));
        // A further 400 would exceed 1000 and the cache is non-empty ⇒ would block.
        // Verified via the blocking test below; here just confirm accounting.
    }

    void OversizedPacketAdmittedWhenEmpty()
    {
        ReadAheadCache c;
        c.Configure(true, 100);
        // A single packet larger than the whole budget must still go through when the
        // cache is empty, otherwise playback would deadlock.
        QVERIFY(c.Reserve(10'000));
    }

    void RemoveBytesFreesSpaceAndUnblocks()
    {
        ReadAheadCache c;
        c.Configure(true, 1000);
        QVERIFY(c.Reserve(1000)); // full

        std::atomic<bool> admitted{false};
        std::thread producer(
            [&]
            {
                // Blocks until space frees up.
                if (c.Reserve(500))
                {
                    admitted = true;
                }
            }
        );

        // Give the producer a moment to park on the cv, then free space.
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        QVERIFY(!(admitted.load()));

        c.RemoveBytes(600); // used 1000 → 400; 400 + 500 <= 1000 ⇒ admit
        producer.join();
        QVERIFY(admitted.load());
    }

    void AbortReleasesWaiter()
    {
        ReadAheadCache c;
        c.Configure(true, 1000);
        QVERIFY(c.Reserve(1000));

        std::atomic<int> result{-1};
        std::thread producer(
            [&]
            {
                result = c.Reserve(500) ? 1 : 0;
            }
        );

        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        c.Abort();
        producer.join();
        QVERIFY((result.load()) == (0)); // aborted ⇒ returns false
    }

    void ResetClearsUsedAndAbort()
    {
        ReadAheadCache c;
        c.Configure(true, 1000);
        QVERIFY(c.Reserve(700));
        c.Abort();
        QVERIFY(!(c.Reserve(1))); // aborted

        c.Reset();
        QVERIFY((c.UsedBytes()) == (0));
        QVERIFY(c.Reserve(500)); // usable again
    }

    void HitMissCounters()
    {
        ReadAheadCache c;
        QVERIFY((c.Hits()) == (0));
        QVERIFY((c.Misses()) == (0));

        c.RecordHit();
        c.RecordHit();
        c.RecordMiss();
        QVERIFY((c.Hits()) == (2));
        QVERIFY((c.Misses()) == (1));

        c.ResetMetrics();
        QVERIFY((c.Hits()) == (0));
        QVERIFY((c.Misses()) == (0));
    }

    void ResetKeepsMetrics()
    {
        ReadAheadCache c;
        c.RecordHit();
        c.RecordMiss();
        c.Reset(); // clears bytes/abort but not metrics
        QVERIFY((c.Hits()) == (1));
        QVERIFY((c.Misses()) == (1));
    }

    void StaleReservationCleanupDoesNotTouchNewEpoch()
    {
        ReadAheadCache c;
        c.Configure(true, 1000);
        std::uint64_t oldEpoch = 0;
        std::uint64_t newEpoch = 0;
        QVERIFY(c.Reserve(400, &oldEpoch));
        c.Reset();
        QVERIFY(c.Reserve(600, &newEpoch));
        QVERIFY(oldEpoch != newEpoch);

        c.RemoveBytes(400, oldEpoch);
        QCOMPARE(c.UsedBytes(), 600);
        c.RemoveBytes(600, newEpoch);
        QCOMPARE(c.UsedBytes(), 0);
    }

    void UsedKbRoundsDown()
    {
        ReadAheadCache c;
        c.Configure(true, 1 << 20);
        QVERIFY(c.Reserve(2048 + 512)); // 2.5 KiB
        QVERIFY((c.UsedKB()) == (2));
    }

    void PeakTracksHighWaterMark()
    {
        ReadAheadCache c;
        c.Configure(true, 1 << 20);

        QVERIFY(c.Reserve(1024));
        QVERIFY(c.Reserve(2048));
        c.RemoveBytes(2500);
        QVERIFY(c.Reserve(512));

        QVERIFY((c.UsedBytes()) == (1084));
        QVERIFY((c.PeakUsedBytes()) == (3072));
        QVERIFY((c.PeakUsedKB()) == (3));
    }

    void ResetMetricsKeepsCurrentUsedAsPeakBaseline()
    {
        ReadAheadCache c;
        c.Configure(true, 1 << 20);
        QVERIFY(c.Reserve(4096));
        c.RecordHit();
        c.ResetMetrics();

        QVERIFY((c.Hits()) == (0));
        QVERIFY((c.PeakUsedBytes()) == (4096));

        c.RemoveBytes(2048);
        QVERIFY((c.PeakUsedBytes()) == (4096));

        QVERIFY(c.Reserve(4096));
        QVERIFY((c.PeakUsedBytes()) == (6144));
    }

    void ConcurrentReservationsStayWithinBudget()
    {
        ReadAheadCache c;
        c.Configure(true, 1000);

        std::barrier start{2};
        std::atomic<int> attempted{0};
        std::atomic<int> reserved{0};
        std::atomic<bool> failed{false};
        std::thread first(
            [&]
            {
                start.arrive_and_wait();
                ++attempted;
                if (c.Reserve(600))
                {
                    ++reserved;
                }
                else
                {
                    failed = true;
                }
            }
        );
        std::thread second(
            [&]
            {
                start.arrive_and_wait();
                ++attempted;
                if (c.Reserve(600))
                {
                    ++reserved;
                }
                else
                {
                    failed = true;
                }
            }
        );

        QTRY_COMPARE_WITH_TIMEOUT(attempted.load(), 2, 1000);
        QTRY_COMPARE_WITH_TIMEOUT(reserved.load(), 1, 1000);
        QCOMPARE(c.UsedBytes(), 600);

        c.RemoveBytes(600);
        first.join();
        second.join();
        QVERIFY(!failed.load());
        QCOMPARE(c.UsedBytes(), 600);
        QCOMPARE(c.PeakUsedBytes(), 600);
    }

    void StallCallbackFiresOnAggregateTransition()
    {
        ReadAheadCache c;
        std::vector<bool> transitions;
        c.SetStallCallback(
            [&](bool s)
            {
                transitions.push_back(s);
            }
        );

        c.BeginStall(); // 0 → 1 : true
        c.BeginStall(); // 1 → 2 : no callback
        QVERIFY(c.Stalling());
        c.EndStall(); // 2 → 1 : no callback
        c.EndStall(); // 1 → 0 : false
        QVERIFY(!(c.Stalling()));

        QVERIFY((transitions.size()) == (2u));
        QVERIFY(transitions[0]);
        QVERIFY(!(transitions[1]));
    }
};

namespace
{
const ::framelift::test::Registrar<ReadAheadCacheTest> kRegisterReadAheadCacheTest{"ReadAheadCacheTest"};
}

#include "ReadAheadCacheTests.moc"
