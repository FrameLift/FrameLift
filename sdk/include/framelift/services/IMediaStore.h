#pragma once

// Shared on-disk media metadata store — one host-owned SQLite database file with
// prepared-statement SQL access for plugins. A capability service — discover it with
// ctx.GetService<IMediaStore>() and null-check before use. For ergonomic call sites,
// prefer the RAII wrappers in <framelift/MediaStoreHelpers.h> over this raw vtable.
//
// Namespacing is a convention, not enforced: every table/index a plugin creates MUST
// be prefixed with its namespace + '_' (e.g. history_entries, tags_labels). Write only
// to your own namespace; reading (including joins) across namespaces is allowed. The
// host is domain-agnostic — it creates no plugin tables and never inspects them.
// Per-namespace schema versions live in the host-owned media_store_schema table:
// GetSchemaVersion returns 0 for a namespace that was never set up, and each plugin
// runs its own CREATE TABLE / migration DDL before bumping its version.
//
// Threading: all methods may be called from any thread; the implementation keeps one
// connection per calling thread against the same database (WAL journal). A statement
// handle is bound to the thread that Prepare()d it — use and Finalize it on that
// thread only. Transactions scope the calling thread's connection.
//
// ABI-safe: statements cross the boundary as opaque handles; values cross as POD /
// const char* (the buf/cap idiom of ISettingsStore). Adding this service does not
// bump FRAMELIFT_ABI_VERSION.
class IMediaStore
{
public:
    static constexpr const char* InterfaceId = "framelift.IMediaStore";
    virtual ~IMediaStore() = default;

    // ── Statements ─────────────────────────────────────────────────────────────
    // Compile a single SQL statement with '?' positional placeholders. Returns an
    // opaque handle, or nullptr on error (see GetLastError). Free with Finalize.
    [[nodiscard]] virtual void* Prepare(const char* sql) noexcept = 0;
    virtual void Finalize(void* stmt) noexcept = 0;

    // Bind the `index`-th (0-based) '?' placeholder. Rebinding after Reset is allowed.
    virtual bool BindText(void* stmt, int index, const char* value) noexcept = 0;
    virtual bool BindDouble(void* stmt, int index, double value) noexcept = 0;
    virtual bool BindInt64(void* stmt, int index, long long value) noexcept = 0;
    virtual bool BindNull(void* stmt, int index) noexcept = 0;

    // Execute / advance: 1 ⇒ a row is available, 0 ⇒ done (no more rows, or the
    // statement produces no result set), -1 ⇒ error (see GetLastError).
    [[nodiscard]] virtual int Step(void* stmt) noexcept = 0;
    // Rewind for re-execution; bindings are kept until re-bound.
    virtual bool Reset(void* stmt) noexcept = 0;

    // ── Column reads (valid after Step returned 1; col is 0-based) ─────────────
    [[nodiscard]] virtual int ColumnCount(const void* stmt) const noexcept = 0;
    // Writes up to cap-1 chars + NUL into buf and returns the full length excl.
    // NUL; pass buf=nullptr to query the required length.
    [[nodiscard]] virtual int ColumnText(void* stmt, int col, char* buf, int cap) noexcept = 0;
    [[nodiscard]] virtual double ColumnDouble(void* stmt, int col) noexcept = 0;
    [[nodiscard]] virtual long long ColumnInt64(void* stmt, int col) noexcept = 0;
    [[nodiscard]] virtual bool ColumnIsNull(void* stmt, int col) noexcept = 0;

    // ── One-shot / bookkeeping ─────────────────────────────────────────────────
    // Execute a statement without reading results (DDL, DELETE, ...). False on error.
    virtual bool Exec(const char* sql) noexcept = 0;
    [[nodiscard]] virtual long long LastInsertId() noexcept = 0;

    // ── Transactions (scoped to the calling thread's connection) ───────────────
    virtual bool Begin() noexcept = 0;
    virtual bool Commit() noexcept = 0;
    virtual void Rollback() noexcept = 0;

    // ── Per-namespace schema versioning ────────────────────────────────────────
    [[nodiscard]] virtual int GetSchemaVersion(const char* ns) noexcept = 0;
    virtual bool SetSchemaVersion(const char* ns, int version) noexcept = 0;

    // Last error message raised on the calling thread (buf/cap idiom).
    [[nodiscard]] virtual int GetLastError(char* buf, int cap) const noexcept = 0;
};
