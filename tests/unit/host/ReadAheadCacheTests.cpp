#include "ReadAheadCache.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

// Pure-logic tests for the shared read-ahead budget + metrics. No
// SDL/FFmpeg — the queues that own AVPackets only feed bytes into this type.

TEST(ReadAheadCacheTest, DisabledAdmitsEverything)
{
    ReadAheadCache c;
    c.Configure(/*enabled=*/false, /*maxBytes=*/1024);
    // Byte-bounding off: WaitForSpace never blocks regardless of used bytes.
    EXPECT_TRUE(c.WaitForSpace(1 << 30));
    c.AddBytes(1 << 30);
    EXPECT_TRUE(c.WaitForSpace(1 << 30));
}

TEST(ReadAheadCacheTest, AdmitsUntilBudgetReached)
{
    ReadAheadCache c;
    c.Configure(true, 1000);

    EXPECT_TRUE(c.WaitForSpace(400));
    c.AddBytes(400);
    EXPECT_TRUE(c.WaitForSpace(400)); // 800 <= 1000
    c.AddBytes(400);
    EXPECT_EQ(c.UsedBytes(), 800);
    // A further 400 would exceed 1000 and the cache is non-empty ⇒ would block.
    // Verified via the blocking test below; here just confirm accounting.
}

TEST(ReadAheadCacheTest, OversizedPacketAdmittedWhenEmpty)
{
    ReadAheadCache c;
    c.Configure(true, 100);
    // A single packet larger than the whole budget must still go through when the
    // cache is empty, otherwise playback would deadlock.
    EXPECT_TRUE(c.WaitForSpace(10'000));
}

TEST(ReadAheadCacheTest, RemoveBytesFreesSpaceAndUnblocks)
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
        });

    // Give the producer a moment to park on the cv, then free space.
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    EXPECT_FALSE(admitted.load());

    c.RemoveBytes(600); // used 1000 → 400; 400 + 500 <= 1000 ⇒ admit
    producer.join();
    EXPECT_TRUE(admitted.load());
}

TEST(ReadAheadCacheTest, AbortReleasesWaiter)
{
    ReadAheadCache c;
    c.Configure(true, 1000);
    c.AddBytes(1000);

    std::atomic<int> result{-1};
    std::thread producer([&] { result = c.WaitForSpace(500) ? 1 : 0; });

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    c.Abort();
    producer.join();
    EXPECT_EQ(result.load(), 0); // aborted ⇒ returns false
}

TEST(ReadAheadCacheTest, ResetClearsUsedAndAbort)
{
    ReadAheadCache c;
    c.Configure(true, 1000);
    c.AddBytes(700);
    c.Abort();
    EXPECT_FALSE(c.WaitForSpace(1)); // aborted

    c.Reset();
    EXPECT_EQ(c.UsedBytes(), 0);
    EXPECT_TRUE(c.WaitForSpace(500)); // usable again
}

TEST(ReadAheadCacheTest, HitMissCounters)
{
    ReadAheadCache c;
    EXPECT_EQ(c.Hits(), 0);
    EXPECT_EQ(c.Misses(), 0);

    c.RecordHit();
    c.RecordHit();
    c.RecordMiss();
    EXPECT_EQ(c.Hits(), 2);
    EXPECT_EQ(c.Misses(), 1);

    c.ResetMetrics();
    EXPECT_EQ(c.Hits(), 0);
    EXPECT_EQ(c.Misses(), 0);
}

TEST(ReadAheadCacheTest, ResetKeepsMetrics)
{
    ReadAheadCache c;
    c.RecordHit();
    c.RecordMiss();
    c.Reset(); // clears bytes/abort but not metrics
    EXPECT_EQ(c.Hits(), 1);
    EXPECT_EQ(c.Misses(), 1);
}

TEST(ReadAheadCacheTest, UsedKbRoundsDown)
{
    ReadAheadCache c;
    c.Configure(true, 1 << 20);
    c.AddBytes(2048 + 512); // 2.5 KiB
    EXPECT_EQ(c.UsedKB(), 2);
}

TEST(ReadAheadCacheTest, StallCallbackFiresOnAggregateTransition)
{
    ReadAheadCache c;
    std::vector<bool> transitions;
    c.SetStallCallback([&](bool s) { transitions.push_back(s); });

    c.BeginStall(); // 0 → 1 : true
    c.BeginStall(); // 1 → 2 : no callback
    EXPECT_TRUE(c.Stalling());
    c.EndStall(); // 2 → 1 : no callback
    c.EndStall(); // 1 → 0 : false
    EXPECT_FALSE(c.Stalling());

    ASSERT_EQ(transitions.size(), 2u);
    EXPECT_TRUE(transitions[0]);
    EXPECT_FALSE(transitions[1]);
}
