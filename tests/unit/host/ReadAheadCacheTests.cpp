#include "ReadAheadCache.h"

#include "QtTestRunner.h"

#include <QtTest/QtTest>

#include <atomic>
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
        // Byte-bounding off: WaitForSpace never blocks regardless of used bytes.
        QVERIFY(c.WaitForSpace(1 << 30));
        c.AddBytes(1 << 30);
        QVERIFY(c.WaitForSpace(1 << 30));
    }

    void AdmitsUntilBudgetReached()
    {
        ReadAheadCache c;
        c.Configure(true, 1000);

        QVERIFY(c.WaitForSpace(400));
        c.AddBytes(400);
        QVERIFY(c.WaitForSpace(400)); // 800 <= 1000
        c.AddBytes(400);
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
        QVERIFY(c.WaitForSpace(10'000));
    }

    void RemoveBytesFreesSpaceAndUnblocks()
    {
        ReadAheadCache c;
        c.Configure(true, 1000);
        c.AddBytes(1000); // full

        std::atomic<bool> admitted{false};
        std::thread producer(
            [&]
            {
                // Blocks until space frees up.
                if (c.WaitForSpace(500))
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
        c.AddBytes(1000);

        std::atomic<int> result{-1};
        std::thread producer(
            [&]
            {
                result = c.WaitForSpace(500) ? 1 : 0;
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
        c.AddBytes(700);
        c.Abort();
        QVERIFY(!(c.WaitForSpace(1))); // aborted

        c.Reset();
        QVERIFY((c.UsedBytes()) == (0));
        QVERIFY(c.WaitForSpace(500)); // usable again
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

    void UsedKbRoundsDown()
    {
        ReadAheadCache c;
        c.Configure(true, 1 << 20);
        c.AddBytes(2048 + 512); // 2.5 KiB
        QVERIFY((c.UsedKB()) == (2));
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
