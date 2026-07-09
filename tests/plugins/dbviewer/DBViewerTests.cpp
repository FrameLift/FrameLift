#include "DBViewer.h"
#include "MediaStoreImpl.h"
#include "TempIni.h"

#include "QtTestRunner.h"

#include <QtTest/QtTest>

#include <filesystem>
#include <string>

// DBViewer reads the shared store through the host IMediaStore service; compile the
// real Qt SQL-backed MediaStoreImpl into the test so table enumeration and result
// projection run against the real backend (as History/AI Tagger tests do).
namespace
{
// Owns a unique directory holding a real store database (WAL adds -wal/-shm siblings,
// so per-file cleanup is not enough); a temp file is required because the store keeps
// one connection per thread and :memory: would give each thread a private database.
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

// Look up the { name, type, rowCount } map for a table in the projection.
QVariantMap TableRow(const DBViewer& v, const QString& name)
{
    for (const QVariant& t : v.QmlTables())
    {
        const QVariantMap m = t.toMap();
        if (m.value(QStringLiteral("name")).toString() == name)
        {
            return m;
        }
    }
    return {};
}
} // namespace

class DBViewerTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void ReadOnlyGuardAcceptsReadsRejectsWrites()
    {
        QVERIFY(DBViewer::IsReadOnly("SELECT 1"));
        QVERIFY(DBViewer::IsReadOnly("  select * from history_entries  "));
        QVERIFY(DBViewer::IsReadOnly("WITH x AS (SELECT 1) SELECT * FROM x"));
        QVERIFY(DBViewer::IsReadOnly("PRAGMA table_info(history_entries)"));
        QVERIFY(DBViewer::IsReadOnly("EXPLAIN SELECT 1"));
        QVERIFY(DBViewer::IsReadOnly("SELECT 1;"));                // single trailing semicolon tolerated
        QVERIFY(DBViewer::IsReadOnly("SELECT is_deleted FROM t")); // 'delete' only as a substring

        QVERIFY(!DBViewer::IsReadOnly(""));
        QVERIFY(!DBViewer::IsReadOnly("DELETE FROM t"));
        QVERIFY(!DBViewer::IsReadOnly("DROP TABLE t"));
        QVERIFY(!DBViewer::IsReadOnly("INSERT INTO t VALUES(1)"));
        QVERIFY(!DBViewer::IsReadOnly("UPDATE t SET a = 1"));
        QVERIFY(!DBViewer::IsReadOnly("SELECT 1; DROP TABLE t"));             // two statements
        QVERIFY(!DBViewer::IsReadOnly("WITH x AS (SELECT 1) DELETE FROM t")); // smuggled write
    }

    void UnavailableWithoutStore()
    {
        DBViewer v;
        QVERIFY(!v.IsAvailable());
        QVERIFY(v.QmlTables().isEmpty());
    }

    void ListsTablesWithCountsExcludingSqliteInternal()
    {
        const TempDb db;
        MediaStoreImpl store(db.qstr());
        QVERIFY(store.Exec("CREATE TABLE alpha(a TEXT, b INTEGER)"));
        QVERIFY(store.Exec("INSERT INTO alpha VALUES('x', 1), ('y', 2)"));
        QVERIFY(store.Exec("CREATE TABLE beta(id INTEGER PRIMARY KEY)"));

        DBViewer v;
        v.SetMediaStore(&store);

        QCOMPARE(TableRow(v, "alpha").value("rowCount").toLongLong(), 2LL);
        QCOMPARE(TableRow(v, "beta").value("rowCount").toLongLong(), 0LL);
        for (const QVariant& t : v.QmlTables())
        {
            QVERIFY(!t.toMap().value("name").toString().startsWith("sqlite_"));
        }
    }

    void SelectTableProjectsColumnsRowsAndNulls()
    {
        const TempDb db;
        MediaStoreImpl store(db.qstr());
        QVERIFY(store.Exec("CREATE TABLE demo(name TEXT, score REAL)"));
        QVERIFY(store.Exec("INSERT INTO demo(name, score) VALUES('a', 1.5)"));
        QVERIFY(store.Exec("INSERT INTO demo(name) VALUES('b')")); // score is NULL

        DBViewer v;
        v.SetMediaStore(&store);
        v.selectTable("demo");

        QVERIFY(v.Error().isEmpty());
        QCOMPARE(v.Columns(), QStringList({"name", "score"}));
        QCOMPARE(v.Rows().size(), 2);
        QCOMPARE(v.Rows()[0].toList()[0].toString(), QString("a"));
        QCOMPARE(v.Rows()[0].toList()[1].toString(), QString("1.5"));
        QCOMPARE(v.Rows()[1].toList()[0].toString(), QString("b"));
        QVERIFY(v.Rows()[1].toList()[1].isNull()); // NULL round-trips as an invalid QVariant
    }

    void SelectUnknownTableSetsError()
    {
        const TempDb db;
        MediaStoreImpl store(db.qstr());
        DBViewer v;
        v.SetMediaStore(&store);

        v.selectTable("does_not_exist");
        QVERIFY(!v.Error().isEmpty());
        QVERIFY(v.Rows().isEmpty());
    }

    void RunQueryRejectsWritesAndLeavesDataIntact()
    {
        const TempDb db;
        MediaStoreImpl store(db.qstr());
        QVERIFY(store.Exec("CREATE TABLE demo(name TEXT)"));
        QVERIFY(store.Exec("INSERT INTO demo VALUES('keep')"));

        DBViewer v;
        v.SetMediaStore(&store);

        v.runQuery("DELETE FROM demo");
        QVERIFY(!v.Error().isEmpty());
        QVERIFY(v.Rows().isEmpty());

        v.selectTable("demo"); // data must be untouched
        QCOMPARE(v.Rows().size(), 1);
        QCOMPARE(v.Rows()[0].toList()[0].toString(), QString("keep"));
    }

    void RunQuerySelectProjectsResult()
    {
        const TempDb db;
        MediaStoreImpl store(db.qstr());
        QVERIFY(store.Exec("CREATE TABLE demo(name TEXT, n INTEGER)"));
        QVERIFY(store.Exec("INSERT INTO demo VALUES('a', 1), ('b', 2)"));

        DBViewer v;
        v.SetMediaStore(&store);
        v.runQuery("SELECT n, name FROM demo WHERE n = 2");

        QVERIFY(v.Error().isEmpty());
        QCOMPARE(v.Columns(), QStringList({"n", "name"}));
        QCOMPARE(v.Rows().size(), 1);
        QCOMPARE(v.Rows()[0].toList()[1].toString(), QString("b"));
    }

    void RowCapTruncatesLargeResults()
    {
        const TempDb db;
        MediaStoreImpl store(db.qstr());
        QVERIFY(store.Exec("CREATE TABLE big(n INTEGER)"));

        DBViewer v; // cap is fixed and independent of the store
        const int cap = v.RowCap();
        QVERIFY(store.Begin());
        for (int i = 0; i < cap + 5; ++i)
        {
            QVERIFY(store.Exec(("INSERT INTO big(n) VALUES(" + std::to_string(i) + ")").c_str()));
        }
        QVERIFY(store.Commit());

        v.SetMediaStore(&store);
        v.selectTable("big");

        QCOMPARE(v.Rows().size(), cap);
        QVERIFY(v.Truncated());
    }
};

namespace
{
const ::framelift::test::Registrar<DBViewerTest> kRegisterDBViewerTest{"DBViewerTest"};
}

#include "DBViewerTests.moc"
