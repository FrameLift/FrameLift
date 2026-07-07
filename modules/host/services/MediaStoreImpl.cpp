#include "MediaStoreImpl.h"

#include <QtCore/QAtomicInteger>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QVariant>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlError>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlRecord>

#include <framelift/Log.h>

#include <cstring>
#include <string>
#include <utility>

namespace
{

// Copy `s` into buf/cap per the ISettingsStore idiom: write ≤cap-1 chars + NUL,
// return the full length excl. NUL; buf=nullptr just queries the length.
int CopyOut(const QByteArray& s, char* buf, int cap) noexcept
{
    const int len = static_cast<int>(s.size());
    if (buf && cap > 0)
    {
        const int n = len < cap - 1 ? len : cap - 1;
        std::memcpy(buf, s.constData(), static_cast<std::size_t>(n));
        buf[n] = '\0';
    }
    return len;
}

// A prepared statement: a QSqlQuery on the preparing thread's connection. The first
// Step after Prepare/Reset executes; later Steps only advance the result set.
struct Stmt
{
    QSqlQuery query;
    bool executed = false;
};

Stmt* AsStmt(void* p) noexcept
{
    return static_cast<Stmt*>(p);
}

const Stmt* AsStmt(const void* p) noexcept
{
    return static_cast<const Stmt*>(p);
}

QAtomicInteger<int> g_connSeq;

} // namespace

// Per-thread connection state. QThreadStorage owns the pointer and deletes it on
// thread exit; the destructor tears down the Qt connection so the name can be reused.
struct MediaStoreImpl::ThreadConn
{
    QString name;
    bool ok = false;
    QString lastError;
    long long lastInsertId = 0;

    ~ThreadConn()
    {
        if (QSqlDatabase::contains(name))
        {
            QSqlDatabase::database(name, /*open=*/false).close();
            QSqlDatabase::removeDatabase(name);
        }
    }
};

MediaStoreImpl::MediaStoreImpl(QString dbPath) : dbPath_(std::move(dbPath))
{
}

MediaStoreImpl::~MediaStoreImpl()
{
    // Sweep connections whose threads have not exited yet (normally just the GUI
    // thread's). ThreadConn destructors that run later find theirs already removed.
    const std::lock_guard<std::mutex> lock(namesMutex_);
    for (const QString& name : connectionNames_)
    {
        if (QSqlDatabase::contains(name))
        {
            QSqlDatabase::database(name, /*open=*/false).close();
            QSqlDatabase::removeDatabase(name);
        }
    }
}

MediaStoreImpl::ThreadConn* MediaStoreImpl::Conn() const noexcept
{
    if (conn_.hasLocalData())
    {
        return conn_.localData();
    }

    auto* conn = new ThreadConn;
    conn->name = QStringLiteral("framelift.mediastore.%1").arg(g_connSeq.fetchAndAddRelaxed(1));
    conn_.setLocalData(conn);
    {
        const std::lock_guard<std::mutex> lock(namesMutex_);
        connectionNames_.push_back(conn->name);
    }

    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE")))
    {
        conn->lastError = QStringLiteral("QSQLITE driver not available");
        Log::Error("MediaStore: QSQLITE driver not available; media store disabled");
        return conn;
    }

    const QFileInfo info(dbPath_);
    if (!info.absolutePath().isEmpty())
    {
        (void)QDir().mkpath(info.absolutePath());
    }

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn->name);
    db.setDatabaseName(dbPath_);
    if (!db.open())
    {
        conn->lastError = db.lastError().text();
        Log::Error("MediaStore: cannot open \"{}\": {}", dbPath_.toStdString(), conn->lastError.toStdString());
        return conn;
    }

    QSqlQuery setup(db);
    (void)setup.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    (void)setup.exec(QStringLiteral("PRAGMA busy_timeout=5000"));
    (void)setup.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
    (void)setup.exec(QStringLiteral("PRAGMA foreign_keys=ON"));
    if (!setup.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS media_store_schema(namespace TEXT PRIMARY KEY, version INTEGER NOT NULL)"
        )))
    {
        conn->lastError = setup.lastError().text();
        Log::Error("MediaStore: schema bootstrap failed: {}", conn->lastError.toStdString());
        db.close();
        return conn;
    }

    conn->ok = true;
    return conn;
}

// ── Statements ─────────────────────────────────────────────────────────────────────

void* MediaStoreImpl::Prepare(const char* sql) noexcept
{
    ThreadConn* conn = Conn();
    if (!conn->ok || !sql)
    {
        return nullptr;
    }
    auto stmt = std::make_unique<Stmt>(QSqlQuery(QSqlDatabase::database(conn->name, /*open=*/false)));
    if (!stmt->query.prepare(QString::fromUtf8(sql)))
    {
        conn->lastError = stmt->query.lastError().text();
        return nullptr;
    }
    return stmt.release();
}

void MediaStoreImpl::Finalize(void* stmt) noexcept
{
    delete AsStmt(stmt);
}

bool MediaStoreImpl::BindText(void* stmt, int index, const char* value) noexcept
{
    if (!stmt)
    {
        return false;
    }
    AsStmt(stmt)->query.bindValue(index, value ? QVariant(QString::fromUtf8(value)) : QVariant());
    return true;
}

bool MediaStoreImpl::BindDouble(void* stmt, int index, double value) noexcept
{
    if (!stmt)
    {
        return false;
    }
    AsStmt(stmt)->query.bindValue(index, value);
    return true;
}

bool MediaStoreImpl::BindInt64(void* stmt, int index, long long value) noexcept
{
    if (!stmt)
    {
        return false;
    }
    AsStmt(stmt)->query.bindValue(index, QVariant::fromValue<qlonglong>(value));
    return true;
}

bool MediaStoreImpl::BindNull(void* stmt, int index) noexcept
{
    if (!stmt)
    {
        return false;
    }
    AsStmt(stmt)->query.bindValue(index, QVariant());
    return true;
}

int MediaStoreImpl::Step(void* stmt) noexcept
{
    if (!stmt)
    {
        return -1;
    }
    Stmt* s = AsStmt(stmt);
    ThreadConn* conn = Conn();
    if (!s->executed)
    {
        if (!s->query.exec())
        {
            conn->lastError = s->query.lastError().text();
            return -1;
        }
        s->executed = true;
        const QVariant id = s->query.lastInsertId();
        if (id.isValid())
        {
            conn->lastInsertId = id.toLongLong();
        }
        if (!s->query.isSelect())
        {
            return 0;
        }
    }
    return s->query.next() ? 1 : 0;
}

bool MediaStoreImpl::Reset(void* stmt) noexcept
{
    if (!stmt)
    {
        return false;
    }
    Stmt* s = AsStmt(stmt);
    s->query.finish(); // keeps the prepared statement and bound values
    s->executed = false;
    return true;
}

// ── Column reads ───────────────────────────────────────────────────────────────────

int MediaStoreImpl::ColumnCount(const void* stmt) const noexcept
{
    return stmt ? AsStmt(stmt)->query.record().count() : 0;
}

int MediaStoreImpl::ColumnText(void* stmt, int col, char* buf, int cap) noexcept
{
    if (!stmt)
    {
        return 0;
    }
    return CopyOut(AsStmt(stmt)->query.value(col).toString().toUtf8(), buf, cap);
}

double MediaStoreImpl::ColumnDouble(void* stmt, int col) noexcept
{
    return stmt ? AsStmt(stmt)->query.value(col).toDouble() : 0.0;
}

long long MediaStoreImpl::ColumnInt64(void* stmt, int col) noexcept
{
    return stmt ? AsStmt(stmt)->query.value(col).toLongLong() : 0;
}

bool MediaStoreImpl::ColumnIsNull(void* stmt, int col) noexcept
{
    return !stmt || AsStmt(stmt)->query.isNull(col);
}

// ── One-shot / bookkeeping ─────────────────────────────────────────────────────────

bool MediaStoreImpl::Exec(const char* sql) noexcept
{
    ThreadConn* conn = Conn();
    if (!conn->ok || !sql)
    {
        return false;
    }
    QSqlQuery query(QSqlDatabase::database(conn->name, /*open=*/false));
    if (!query.exec(QString::fromUtf8(sql)))
    {
        conn->lastError = query.lastError().text();
        return false;
    }
    const QVariant id = query.lastInsertId();
    if (id.isValid())
    {
        conn->lastInsertId = id.toLongLong();
    }
    return true;
}

long long MediaStoreImpl::LastInsertId() noexcept
{
    return Conn()->lastInsertId;
}

// ── Transactions ───────────────────────────────────────────────────────────────────

bool MediaStoreImpl::Begin() noexcept
{
    ThreadConn* conn = Conn();
    if (!conn->ok)
    {
        return false;
    }
    QSqlDatabase db = QSqlDatabase::database(conn->name, /*open=*/false);
    if (!db.transaction())
    {
        conn->lastError = db.lastError().text();
        return false;
    }
    return true;
}

bool MediaStoreImpl::Commit() noexcept
{
    ThreadConn* conn = Conn();
    if (!conn->ok)
    {
        return false;
    }
    QSqlDatabase db = QSqlDatabase::database(conn->name, /*open=*/false);
    if (!db.commit())
    {
        conn->lastError = db.lastError().text();
        return false;
    }
    return true;
}

void MediaStoreImpl::Rollback() noexcept
{
    ThreadConn* conn = Conn();
    if (conn->ok)
    {
        (void)QSqlDatabase::database(conn->name, /*open=*/false).rollback();
    }
}

// ── Schema versions ────────────────────────────────────────────────────────────────

int MediaStoreImpl::GetSchemaVersion(const char* ns) noexcept
{
    ThreadConn* conn = Conn();
    if (!conn->ok || !ns)
    {
        return 0;
    }
    QSqlQuery query(QSqlDatabase::database(conn->name, /*open=*/false));
    if (!query.prepare(QStringLiteral("SELECT version FROM media_store_schema WHERE namespace = ?")))
    {
        conn->lastError = query.lastError().text();
        return 0;
    }
    query.bindValue(0, QString::fromUtf8(ns));
    if (!query.exec())
    {
        conn->lastError = query.lastError().text();
        return 0;
    }
    return query.next() ? query.value(0).toInt() : 0;
}

bool MediaStoreImpl::SetSchemaVersion(const char* ns, int version) noexcept
{
    ThreadConn* conn = Conn();
    if (!conn->ok || !ns)
    {
        return false;
    }
    QSqlQuery query(QSqlDatabase::database(conn->name, /*open=*/false));
    if (!query.prepare(QStringLiteral(
            "INSERT INTO media_store_schema(namespace, version) VALUES(?, ?) "
            "ON CONFLICT(namespace) DO UPDATE SET version = excluded.version"
        )))
    {
        conn->lastError = query.lastError().text();
        return false;
    }
    query.bindValue(0, QString::fromUtf8(ns));
    query.bindValue(1, version);
    if (!query.exec())
    {
        conn->lastError = query.lastError().text();
        return false;
    }
    return true;
}

int MediaStoreImpl::GetLastError(char* buf, int cap) const noexcept
{
    return CopyOut(Conn()->lastError.toUtf8(), buf, cap);
}
