#pragma once

#include <framelift/core.h>
#include <framelift/services.h>

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVariantList>

#include <string>
#include <vector>

// Developer/diagnostic panel (right edge) that inspects the shared media store
// (the host-owned SQLite database behind IMediaStore). It enumerates the tables in
// the database, browses any table's rows in a scrollable grid, and runs ad-hoc
// read-only SQL. Ships disabled by default (opt in via plugins.ini) — it is a tool
// for looking at what History, the AI Tagger, and future plugins persist.
//
// Read-only is enforced by policy at this plugin (a keyword allow-list + single
// statement), not by the connection, which stays writable for the plugins that own it.
class DBViewer : public QObject, public ModuleBase
{
    Q_OBJECT
    Q_PROPERTY(bool open READ IsOpen NOTIFY panelStateChanged)
    // True once a media store was discovered; false shows an "unavailable" state.
    Q_PROPERTY(bool available READ IsAvailable NOTIFY tablesChanged)
    // [{ name, type, rowCount }] for every table/view in the database.
    Q_PROPERTY(QVariantList tables READ QmlTables NOTIFY tablesChanged)
    // Column headers of the current result set.
    Q_PROPERTY(QStringList columns READ Columns NOTIFY resultChanged)
    // Rows of the current result set; each row is a QVariantList of cell strings.
    Q_PROPERTY(QVariantList rows READ Rows NOTIFY resultChanged)
    // Name of the table currently browsed, or the echoed query; empty on error/none.
    Q_PROPERTY(QString source READ Source NOTIFY resultChanged)
    // Last error message (empty when the last query succeeded).
    Q_PROPERTY(QString error READ Error NOTIFY resultChanged)
    // True when the result was clipped to rowCap rows.
    Q_PROPERTY(bool truncated READ Truncated NOTIFY resultChanged)
    Q_PROPERTY(int rowCap READ RowCap CONSTANT)

public:
    DBViewer();
    ~DBViewer() override;

    // ── IModule ───────────────────────────────────────────────────────────────
    bool HandleKeyDownEvent(const AppEvent& e) override;

    // Inject the host media store and load the table list. Set from OnInstall in
    // production; tests inject a MediaStoreImpl over a temp database directly.
    void SetMediaStore(IMediaStore* store);

    [[nodiscard]] bool IsOpen() const
    {
        return open_;
    }

    [[nodiscard]] bool IsAvailable() const
    {
        return store_ != nullptr;
    }

    [[nodiscard]] QVariantList QmlTables() const
    {
        return tables_;
    }

    [[nodiscard]] QStringList Columns() const
    {
        return columns_;
    }

    [[nodiscard]] QVariantList Rows() const
    {
        return rows_;
    }

    [[nodiscard]] QString Source() const
    {
        return source_;
    }

    [[nodiscard]] QString Error() const
    {
        return error_;
    }

    [[nodiscard]] bool Truncated() const
    {
        return truncated_;
    }

    [[nodiscard]] int RowCap() const
    {
        return rowCap_;
    }

    Q_INVOKABLE void togglePanel();
    Q_INVOKABLE void publishVisibleWidth(qreal width);
    // Re-read the list of tables (other plugins' writes appear on the next read).
    Q_INVOKABLE void refresh();
    // Browse a table: SELECT * FROM "<name>", capped at rowCap rows. `name` is
    // validated against the live table list (identifiers can't be bound in SQLite).
    Q_INVOKABLE void selectTable(const QString& name);
    // Run an ad-hoc query; rejected unless it is a single read-only statement.
    Q_INVOKABLE void runQuery(const QString& sql);

    // True iff `sql` is a single statement beginning with an allow-listed read
    // keyword (SELECT/WITH/PRAGMA/EXPLAIN) and containing no write keyword.
    [[nodiscard]] static bool IsReadOnly(const QString& sql);

protected:
    const char* ModuleName() const override
    {
        return "DBViewer";
    }

    std::vector<framelift::Keybind> Keybinds() override;
    void OnInstall(IModuleContext& ctx) override;

Q_SIGNALS:
    void panelStateChanged();
    void tablesChanged();
    void resultChanged();

private:
    // Re-read sqlite_master into tables_ (with per-table COUNT(*)), then emit.
    void ReloadTables();
    // Execute `sql` (already vetted for browse; runQuery vets read-only first),
    // filling columns_/rows_/error_/truncated_. Emits resultChanged.
    void RunSql(const QString& sql, const QString& source);
    // Copy the store's last error into error_.
    void CaptureError();

    bool open_ = false;
    std::string togglePanelKey_ = "D";
    IMediaStore* store_ = nullptr; // host-owned; not owned here
    int rowCap_ = 500;

    QVariantList tables_; // [{ name, type, rowCount }]
    QStringList columns_; // current result columns
    QVariantList rows_;   // current result rows (each a QVariantList of strings)
    QString source_;      // table name or echoed query for the current result
    QString error_;       // last error, empty on success
    bool truncated_ = false;
};

FRAMELIFT_MODULE_ENTRY(
    DBViewer, {
                  .renderOrder = 30,
              }
)
