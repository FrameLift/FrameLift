#include "MediaStoreImpl.h"
#include "TempIni.h"

#include "QtTestRunner.h"

#include <framelift/MediaStoreHelpers.h>

#include <QtTest/QtTest>

#include <filesystem>
#include <string>
#include <thread>

// The store keeps one SQLite connection per calling thread, so tests must use a real
// temp file — with :memory: every thread would silently get its own private database.
namespace
{
// Owns a unique directory holding the database (WAL mode adds -wal/-shm siblings,
// so per-file cleanup is not enough). The store creates the directory on first open.
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

std::string LastError(const IMediaStore& store)
{
    char buf[512] = {};
    (void)store.GetLastError(buf, sizeof(buf));
    return buf;
}
} // namespace

class MediaStoreTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void PrepareBindStepColumnRoundTrip()
    {
        const TempDb db;
        MediaStoreImpl store(db.qstr());

        QVERIFY(store.Exec("CREATE TABLE demo_items(name TEXT PRIMARY KEY, score REAL, count INTEGER)"));

        framelift::SqlStmt insert(store, "INSERT INTO demo_items(name, score, count) VALUES(?, ?, ?)");
        QVERIFY(static_cast<bool>(insert));
        QVERIFY(insert.bind(0, "alpha"));
        QVERIFY(insert.bind(1, 0.5));
        QVERIFY(insert.bind(2, static_cast<long long>(3)));
        QCOMPARE(insert.step(), 0);

        framelift::SqlStmt select(store, "SELECT name, score, count FROM demo_items WHERE name = ?");
        QVERIFY(select.bind(0, "alpha"));
        QCOMPARE(select.step(), 1);
        QCOMPARE(select.str(0), std::string("alpha"));
        QCOMPARE(select.num(1), 0.5);
        QCOMPARE(select.integer(2), 3LL);
        QCOMPARE(store.ColumnCount(nullptr), 0);
        QCOMPARE(select.step(), 0);
    }

    void ResetReExecutesWithNewBindings()
    {
        const TempDb db;
        MediaStoreImpl store(db.qstr());
        QVERIFY(store.Exec("CREATE TABLE demo_items(name TEXT)"));
        QVERIFY(store.Exec("INSERT INTO demo_items(name) VALUES('a'), ('b')"));

        framelift::SqlStmt select(store, "SELECT name FROM demo_items WHERE name = ?");
        QVERIFY(select.bind(0, "a"));
        QCOMPARE(select.step(), 1);
        QVERIFY(select.reset());
        QVERIFY(select.bind(0, "b"));
        QCOMPARE(select.step(), 1);
        QCOMPARE(select.str(0), std::string("b"));
    }

    void BadSqlFailsWithError()
    {
        const TempDb db;
        MediaStoreImpl store(db.qstr());

        QVERIFY(!store.Exec("NOT ACTUAL SQL"));
        QVERIFY(!LastError(store).empty());
        QCOMPARE(store.Prepare("SELECT * FROM missing_table"), nullptr);
        QVERIFY(!LastError(store).empty());
    }

    void NullColumnReadsAreSafe()
    {
        const TempDb db;
        MediaStoreImpl store(db.qstr());
        QVERIFY(store.Exec("CREATE TABLE demo_items(name TEXT, score REAL)"));

        framelift::SqlStmt insert(store, "INSERT INTO demo_items(name, score) VALUES(?, ?)");
        QVERIFY(insert.bind(0, "x"));
        QVERIFY(insert.bindNull(1));
        QCOMPARE(insert.step(), 0);

        framelift::SqlStmt select(store, "SELECT score FROM demo_items WHERE name = 'x'");
        QCOMPARE(select.step(), 1);
        QVERIFY(select.isNull(0));
        QCOMPARE(select.num(0), 0.0);
    }

    void TransactionsCommitAndRollBack()
    {
        const TempDb db;
        MediaStoreImpl store(db.qstr());
        QVERIFY(store.Exec("CREATE TABLE demo_items(name TEXT)"));

        QVERIFY(store.Begin());
        QVERIFY(store.Exec("INSERT INTO demo_items(name) VALUES('kept')"));
        QVERIFY(store.Commit());

        QVERIFY(store.Begin());
        QVERIFY(store.Exec("INSERT INTO demo_items(name) VALUES('dropped')"));
        store.Rollback();

        framelift::SqlStmt count(store, "SELECT COUNT(*) FROM demo_items");
        QCOMPARE(count.step(), 1);
        QCOMPARE(count.integer(0), 1LL);
    }

    void SchemaVersionsArePerNamespace()
    {
        const TempDb db;
        MediaStoreImpl store(db.qstr());

        QCOMPARE(store.GetSchemaVersion("history"), 0);
        QVERIFY(store.SetSchemaVersion("history", 1));
        QVERIFY(store.SetSchemaVersion("tags", 4));
        QCOMPARE(store.GetSchemaVersion("history"), 1);
        QCOMPARE(store.GetSchemaVersion("tags"), 4);
        QVERIFY(store.SetSchemaVersion("history", 2));
        QCOMPARE(store.GetSchemaVersion("history"), 2);
    }

    void LastInsertIdTracksRowids()
    {
        const TempDb db;
        MediaStoreImpl store(db.qstr());
        QVERIFY(store.Exec("CREATE TABLE demo_items(id INTEGER PRIMARY KEY, name TEXT)"));
        QVERIFY(store.Exec("INSERT INTO demo_items(name) VALUES('first')"));
        QCOMPARE(store.LastInsertId(), 1LL);
        QVERIFY(store.Exec("INSERT INTO demo_items(name) VALUES('second')"));
        QCOMPARE(store.LastInsertId(), 2LL);
    }

    void CrossThreadWritesAreVisible()
    {
        const TempDb db;
        MediaStoreImpl store(db.qstr());
        QVERIFY(store.Exec("CREATE TABLE demo_items(name TEXT)"));

        bool workerOk = false;
        std::thread worker(
            [&store, &workerOk]
            {
                framelift::SqlStmt insert(store, "INSERT INTO demo_items(name) VALUES(?)");
                workerOk = insert.bind(0, "from-worker") && insert.step() == 0;
            }
        );
        worker.join();
        QVERIFY(workerOk);

        framelift::SqlStmt select(store, "SELECT name FROM demo_items");
        QCOMPARE(select.step(), 1);
        QCOMPARE(select.str(0), std::string("from-worker"));
    }
};

namespace
{
const ::framelift::test::Registrar<MediaStoreTest> kRegisterMediaStoreTest{"MediaStoreTest"};
}

#include "MediaStoreTests.moc"
