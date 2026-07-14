#include "History.h"
#include "MediaStoreImpl.h"
#include "TempIni.h"

#include "QtTestRunner.h"

#include <QtCore/QVariantMap>
#include <QtTest/QtTest>

#include <filesystem>
#include <string>

namespace
{
// Owns a unique directory holding a real Qt SQL-backed store database (WAL mode adds
// -wal/-shm siblings, so per-file cleanup is not enough). The store creates the
// directory on first open. A temp file is required — the store keeps one connection
// per calling thread, and :memory: would give each thread a private database.
class TempDb
{
public:
    ~TempDb()
    {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

    [[nodiscard]] QString qstr() const
    {
        return QString::fromStdString((dir / "media.db").string());
    }

    std::filesystem::path dir = UniqueTempPath();
};

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

    void QmlEntriesSeparatePlayedAndResumeMetadata()
    {
        History h;
        h.AddEntry("/movies/a.mp4");
        h.UpdateResumePos("/movies/a.mp4", 65.0);

        const QVariantList rows = h.QmlEntries();
        QCOMPARE(rows.size(), 1);
        const QVariantMap row = rows.front().toMap();
        QCOMPARE(row.value("label").toString(), QStringLiteral("a.mp4"));
        QCOMPARE(row.value("directory").toString(), QStringLiteral("/movies"));
        QVERIFY(!row.value("playedAt").toString().isEmpty());
        QCOMPARE(row.value("resumeText").toString(), QStringLiteral("1:05"));
        QVERIFY(!row.contains("meta"));
    }

    void SearchKeepsTotalCountSeparateFromResults()
    {
        History h;
        h.AddEntry("/movies/alpha.mp4");
        h.AddEntry("/shows/beta.mkv");
        QCOMPARE(h.TotalCount(), 2);

        h.SetSearch(QStringLiteral("MOVIES"));
        QCOMPARE(h.QmlEntries().size(), 1);
        QCOMPARE(h.TotalCount(), 2);

        h.SetSearch(QString());
        QCOMPARE(h.QmlEntries().size(), 2);
        QCOMPARE(h.TotalCount(), 2);
    }

    void ClearAlsoResetsSearchState()
    {
        History h;
        h.AddEntry("/movies/a.mp4");
        h.SetSearch(QStringLiteral("missing"));

        h.Clear();

        QCOMPARE(h.TotalCount(), 0);
        QVERIFY(h.Search().isEmpty());
        QVERIFY(h.QmlEntries().isEmpty());
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

    void LoadsEntriesFromStore()
    {
        const TempDb db;
        MediaStoreImpl store(db.qstr());
        QVERIFY(store.Exec(
            "CREATE TABLE history_entries ("
            "    path            TEXT PRIMARY KEY,"
            "    resume_pos      REAL    NOT NULL DEFAULT 0,"
            "    last_played_utc INTEGER NOT NULL,"
            "    play_count      INTEGER NOT NULL DEFAULT 1)"
        ));
        QVERIFY(store.Exec(
            "INSERT INTO history_entries(path, resume_pos, last_played_utc) "
            "VALUES('/x/y.mp4', 42.5, 1700000000)"
        ));

        History h;
        h.SetMediaStore(&store); // triggers EnsureSchema() + Load()

        QVERIFY((MostRecent(h)) == ("/x/y.mp4"));
        QCOMPARE(h.GetResumePos("/x/y.mp4"), 42.5);
    }

    void PersistsAcrossReload()
    {
        const TempDb db;
        MediaStoreImpl store(db.qstr());

        History h;
        h.SetMediaStore(&store);
        h.AddEntry("/a.mp4"); // writes through synchronously
        h.UpdateResumePos("/a.mp4", 7.5);

        History reload;
        reload.SetMediaStore(&store);
        QVERIFY((MostRecent(reload)) == ("/a.mp4"));
        QCOMPARE(reload.GetResumePos("/a.mp4"), 7.5);
    }

    void ReloadAfterManyAddsKeepsOrderAndResume()
    {
        const TempDb db;
        MediaStoreImpl store(db.qstr());

        History h;
        h.SetMediaStore(&store);
        for (int i = 0; i < 20; ++i)
        {
            h.AddEntry(("/f" + std::to_string(i) + ".mp4").c_str());
        }
        h.UpdateResumePos("/f3.mp4", 33.0);

        History reload;
        reload.SetMediaStore(&store);
        QVERIFY((MostRecent(reload)) == ("/f19.mp4"));
        QCOMPARE(reload.GetResumePos("/f3.mp4"), 33.0);
    }

    void ClearEmptiesTheStore()
    {
        const TempDb db;
        MediaStoreImpl store(db.qstr());

        History h;
        h.SetMediaStore(&store);
        h.AddEntry("/a.mp4");
        h.Clear();

        History reload;
        reload.SetMediaStore(&store);
        QVERIFY((MostRecent(reload)) == (""));
    }
};

namespace
{
const ::framelift::test::Registrar<HistoryTest> kRegisterHistoryTest{"HistoryTest"};
}

#include "HistoryTests.moc"
