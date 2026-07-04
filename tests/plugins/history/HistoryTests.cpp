#include "History.h"
#include "JsonServiceImpl.h"
#include "TempIni.h"

#include "QtTestRunner.h"
#include <chrono>

#include <QtTest/QtTest>
#include <thread>

namespace
{
// Real QJson-backed host JSON service shared by the persistence tests. The service
// object is stateless, so a single instance is safe across the suite (including
// detached writer threads).
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

class HistoryTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void EmptyHasNoMostRecent()
    {
        const History h;
        QVERIFY((h.GetMostRecent(nullptr, 0)) == (0));
        QVERIFY((MostRecent(h)) == (""));
    }

    void AddEntrySetsMostRecent()
    {
        History h;
        h.AddEntry("/movies/a.mp4");
        QVERIFY((MostRecent(h)) == ("/movies/a.mp4"));
        h.AddEntry("/movies/b.mkv");
        QVERIFY((MostRecent(h)) == ("/movies/b.mkv"));
    }

    void AddEntryIgnoresNullPath()
    {
        History h;
        h.AddEntry("/a.mp4");
        h.AddEntry(nullptr); // FileOpenedEvent::path defaults to null — a no-op, not UB
        QVERIFY((MostRecent(h)) == ("/a.mp4"));
    }

    void ReAddingDeduplicatesAndMovesToFront()
    {
        History h;
        h.AddEntry("/a.mp4");
        h.AddEntry("/b.mp4");
        h.AddEntry("/a.mp4"); // re-add → front, no duplicate
        QVERIFY((MostRecent(h)) == ("/a.mp4"));
    }

    void ResumePositionRoundTrips()
    {
        History h;
        h.AddEntry("/a.mp4");
        h.UpdateResumePos("/a.mp4", 42.5);
        QCOMPARE(h.GetResumePos("/a.mp4"), 42.5);
        QCOMPARE(h.GetResumePos("/missing.mp4"), 0.0); // unknown → 0
    }

    void ResumePositionSurvivesReAdd()
    {
        History h;
        h.AddEntry("/a.mp4");
        h.UpdateResumePos("/a.mp4", 12.0);
        h.AddEntry("/b.mp4");
        h.AddEntry("/a.mp4"); // dedup must preserve the saved resume position
        QCOMPARE(h.GetResumePos("/a.mp4"), 12.0);
    }

    void CapsAtMaxEntries()
    {
        History h; // default maxEntries_ == 200
        h.AddEntry("/old.mp4");
        h.UpdateResumePos("/old.mp4", 99.0);

        for (int i = 0; i < 199; ++i) // total 200 — still within cap
        {
            h.AddEntry(("/f" + std::to_string(i) + ".mp4").c_str());
        }
        QCOMPARE(h.GetResumePos("/old.mp4"), 99.0); // still present

        h.AddEntry("/overflow.mp4");               // total 201 → oldest ("/old.mp4") evicted
        QCOMPARE(h.GetResumePos("/old.mp4"), 0.0); // gone
    }

    void LoadsEntriesFromJson()
    {
        const TempFile f(R"([{"p":"/x/y.mp4","r":42.5,"d":"2024-01-01 00:00:00"}])");

        History h;
        h.SetJsonService(&g_json);
        h.SetStoragePath(f.str()); // triggers Load()

        QVERIFY((MostRecent(h)) == ("/x/y.mp4"));
        QCOMPARE(h.GetResumePos("/x/y.mp4"), 42.5);
    }

    void SaveRoundTripsToDisk()
    {
        const TempFile f; // owns a unique path; not yet written

        History h;
        h.SetJsonService(&g_json);
        h.SetStoragePath(f.str()); // empty load (file absent)
        h.AddEntry("/a.mp4");      // triggers Save() on a detached thread

        QVERIFY((WaitForPersistedMostRecent(f.str(), "/a.mp4")) == ("/a.mp4"));
    }

    // Several rapid saves race on the detached writer thread; atomic temp-file +
    // rename must guarantee the destination is always a complete, parseable file
    // (never truncated/empty), so a reload recovers the latest entry intact.
    void ConcurrentSavesNeverCorruptFile()
    {
        const TempFile f;

        History h;
        h.SetJsonService(&g_json);
        h.SetStoragePath(f.str());
        for (int i = 0; i < 20; ++i)
        {
            h.AddEntry(("/f" + std::to_string(i) + ".mp4").c_str());
        }

        QVERIFY((WaitForPersistedMostRecent(f.str(), "/f19.mp4")) == ("/f19.mp4"));
    }
};

namespace
{
const ::framelift::test::Registrar<HistoryTest> kRegisterHistoryTest{"HistoryTest"};
}

#include "HistoryTests.moc"
