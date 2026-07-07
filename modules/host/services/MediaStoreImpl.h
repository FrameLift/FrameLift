#pragma once
#include <framelift/services/IMediaStore.h>

#include <QtCore/QString>
#include <QtCore/QThreadStorage>

#include <mutex>
#include <vector>

// Host media store backed by Qt SQL over one SQLite database file — the single place
// the storage backend is chosen. Plugins reach it via ctx.GetService<IMediaStore>()
// and never link a database library themselves.
//
// QSqlDatabase connections are only usable on the thread that created them, so the
// service keeps one lazily-opened named connection per calling thread (WAL journal
// makes concurrent per-thread readers/writers safe; SQLite arbitrates). Statement
// handles wrap a QSqlQuery on the preparing thread's connection. Last-error state is
// per thread. If the QSQLITE driver is unavailable or the file cannot be opened,
// every method fails soft (nullptr/false/-1) after logging once.
class MediaStoreImpl final : public IMediaStore
{
public:
    // `dbPath` is the SQLite file (parent directories are created on first open) —
    // injectable so tests can point the store at a temp file.
    explicit MediaStoreImpl(QString dbPath);
    ~MediaStoreImpl() override;
    MediaStoreImpl(const MediaStoreImpl&) = delete;
    MediaStoreImpl& operator=(const MediaStoreImpl&) = delete;

    // ── Statements ──
    void* Prepare(const char* sql) noexcept override;
    void Finalize(void* stmt) noexcept override;
    bool BindText(void* stmt, int index, const char* value) noexcept override;
    bool BindDouble(void* stmt, int index, double value) noexcept override;
    bool BindInt64(void* stmt, int index, long long value) noexcept override;
    bool BindNull(void* stmt, int index) noexcept override;
    int Step(void* stmt) noexcept override;
    bool Reset(void* stmt) noexcept override;

    // ── Column reads ──
    int ColumnCount(const void* stmt) const noexcept override;
    int ColumnText(void* stmt, int col, char* buf, int cap) noexcept override;
    double ColumnDouble(void* stmt, int col) noexcept override;
    long long ColumnInt64(void* stmt, int col) noexcept override;
    bool ColumnIsNull(void* stmt, int col) noexcept override;

    // ── One-shot / bookkeeping ──
    bool Exec(const char* sql) noexcept override;
    long long LastInsertId() noexcept override;

    // ── Transactions ──
    bool Begin() noexcept override;
    bool Commit() noexcept override;
    void Rollback() noexcept override;

    // ── Schema versions ──
    int GetSchemaVersion(const char* ns) noexcept override;
    bool SetSchemaVersion(const char* ns, int version) noexcept override;

    int GetLastError(char* buf, int cap) const noexcept override;

private:
    struct ThreadConn;

    // The calling thread's connection state, opened on first use. Never nullptr, but
    // `ok` is false when the driver is missing or the database failed to open.
    ThreadConn* Conn() const noexcept;

    QString dbPath_;
    mutable QThreadStorage<ThreadConn*> conn_;
    mutable std::mutex namesMutex_;
    mutable std::vector<QString> connectionNames_; // for the destructor sweep
};
