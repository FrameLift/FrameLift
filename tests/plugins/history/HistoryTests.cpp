#include "History.h"
#include "JsonServiceImpl.h"
#include "TempIni.h"

#include <chrono>
#include <gtest/gtest.h>
#include <thread>

namespace
{
// Real nlohmann-backed host JSON service shared by the persistence tests. Stateless,
// so a single instance is safe across the suite (including detached writer threads).
JsonServiceImpl g_json;

std::string MostRecent(const History& h)
{
    const int n = h.GetMostRecent(nullptr, 0);
    std::string s(static_cast<std::size_t>(n), '\0');
    if (n > 0)
    {
        h.GetMostRecent(s.data(), n + 1);
    }
    return s;
}

// Save() persists on a detached thread; reload into a fresh History until the
// most-recent entry matches (or time out). Returns the loaded most-recent path.
std::string WaitForPersistedMostRecent(const std::string& path, const std::string& expected)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    std::string got;
    do
    {
        History reload;
        reload.SetJsonService(&g_json);
        reload.SetStoragePath(path);
        got = MostRecent(reload);
        if (got == expected)
        {
            return got;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < deadline);
    return got;
}
} // namespace

TEST(HistoryTest, EmptyHasNoMostRecent)
{
    const History h;
    EXPECT_EQ(h.GetMostRecent(nullptr, 0), 0);
    EXPECT_EQ(MostRecent(h), "");
}

TEST(HistoryTest, AddEntrySetsMostRecent)
{
    History h;
    h.AddEntry("/movies/a.mp4");
    EXPECT_EQ(MostRecent(h), "/movies/a.mp4");
    h.AddEntry("/movies/b.mkv");
    EXPECT_EQ(MostRecent(h), "/movies/b.mkv");
}

TEST(HistoryTest, ReAddingDeduplicatesAndMovesToFront)
{
    History h;
    h.AddEntry("/a.mp4");
    h.AddEntry("/b.mp4");
    h.AddEntry("/a.mp4"); // re-add → front, no duplicate
    EXPECT_EQ(MostRecent(h), "/a.mp4");
}

TEST(HistoryTest, ResumePositionRoundTrips)
{
    History h;
    h.AddEntry("/a.mp4");
    h.UpdateResumePos("/a.mp4", 42.5);
    EXPECT_DOUBLE_EQ(h.GetResumePos("/a.mp4"), 42.5);
    EXPECT_DOUBLE_EQ(h.GetResumePos("/missing.mp4"), 0.0); // unknown → 0
}

TEST(HistoryTest, ResumePositionSurvivesReAdd)
{
    History h;
    h.AddEntry("/a.mp4");
    h.UpdateResumePos("/a.mp4", 12.0);
    h.AddEntry("/b.mp4");
    h.AddEntry("/a.mp4"); // dedup must preserve the saved resume position
    EXPECT_DOUBLE_EQ(h.GetResumePos("/a.mp4"), 12.0);
}

TEST(HistoryTest, CapsAtMaxEntries)
{
    History h; // default maxEntries_ == 200
    h.AddEntry("/old.mp4");
    h.UpdateResumePos("/old.mp4", 99.0);

    for (int i = 0; i < 199; ++i) // total 200 — still within cap
    {
        h.AddEntry(("/f" + std::to_string(i) + ".mp4").c_str());
    }
    EXPECT_DOUBLE_EQ(h.GetResumePos("/old.mp4"), 99.0); // still present

    h.AddEntry("/overflow.mp4");                       // total 201 → oldest ("/old.mp4") evicted
    EXPECT_DOUBLE_EQ(h.GetResumePos("/old.mp4"), 0.0); // gone
}

TEST(HistoryTest, LoadsEntriesFromJson)
{
    const TempFile f(R"([{"p":"/x/y.mp4","r":42.5,"d":"2024-01-01 00:00:00"}])");

    History h;
    h.SetJsonService(&g_json);
    h.SetStoragePath(f.str()); // triggers Load()

    EXPECT_EQ(MostRecent(h), "/x/y.mp4");
    EXPECT_DOUBLE_EQ(h.GetResumePos("/x/y.mp4"), 42.5);
}

TEST(HistoryTest, SaveRoundTripsToDisk)
{
    const TempFile f; // owns a unique path; not yet written

    History h;
    h.SetJsonService(&g_json);
    h.SetStoragePath(f.str()); // empty load (file absent)
    h.AddEntry("/a.mp4");      // triggers Save() on a detached thread

    EXPECT_EQ(WaitForPersistedMostRecent(f.str(), "/a.mp4"), "/a.mp4");
}

// Several rapid saves race on the detached writer thread; atomic temp-file +
// rename must guarantee the destination is always a complete, parseable file
// (never truncated/empty), so a reload recovers the latest entry intact.
TEST(HistoryTest, ConcurrentSavesNeverCorruptFile)
{
    const TempFile f;

    History h;
    h.SetJsonService(&g_json);
    h.SetStoragePath(f.str());
    for (int i = 0; i < 20; ++i)
    {
        h.AddEntry(("/f" + std::to_string(i) + ".mp4").c_str());
    }

    EXPECT_EQ(WaitForPersistedMostRecent(f.str(), "/f19.mp4"), "/f19.mp4");
}
