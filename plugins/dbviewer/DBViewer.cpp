#include "DBViewer.h"

#include <framelift/MediaStoreHelpers.h>
#include <framelift/core.h>

#include <QtCore/QRegularExpression>
#include <QtCore/QVariantMap>

// ── Construction ────────────────────────────────────────────────────────────────

DBViewer::DBViewer() = default;
DBViewer::~DBViewer() = default;

// ── ModuleBase hooks ────────────────────────────────────────────────────────────

std::vector<framelift::Keybind> DBViewer::Keybinds()
{
    return {
        {"Toggle database viewer", "toggleDbViewer", &togglePanelKey_, "F11", [this]
         {
             togglePanel();
         }}
    };
}

void DBViewer::OnInstall(IModuleContext& ctx)
{
    // Discover the shared media store; without it the panel shows an empty state.
    if (!store_)
    {
        SetMediaStore(ctx.GetService<IMediaStore>());
    }
}

bool DBViewer::HandleKeyDownEvent(const AppEvent& e)
{
    if (!IsOpen())
    {
        return false;
    }
    const AppEvent::KeyPayload& kp = e.AsKey();
    if (kp.mods != Mod::None)
    {
        return false;
    }
    // Swallow list-navigation keys so global player hotkeys don't fire while the
    // panel is open; the key still reaches the focused QML item.
    if (kp.key == Keys::Up || kp.key == Keys::Down || kp.key == Keys::Return)
    {
        return true;
    }
    return false;
}

// ── Media store ─────────────────────────────────────────────────────────────────

void DBViewer::SetMediaStore(IMediaStore* store)
{
    store_ = store;
    ReloadTables();
}

void DBViewer::CaptureError()
{
    error_.clear();
    if (!store_)
    {
        return;
    }
    char buf[512] = {};
    if (store_->GetLastError(buf, sizeof(buf)) > 0)
    {
        error_ = QString::fromUtf8(buf);
    }
}

void DBViewer::ReloadTables()
{
    tables_.clear();
    if (store_)
    {
        // First-column ordering keeps tables and views interleaved alphabetically.
        framelift::SqlStmt list(
            *store_, "SELECT name, type FROM sqlite_master "
                     "WHERE type IN ('table','view') AND name NOT LIKE 'sqlite_%' ORDER BY name"
        );
        while (list.step() == 1)
        {
            const std::string name = list.str(0);
            const std::string type = list.str(1);
            long long rowCount = -1; // views / errors report an unknown (-1) count
            if (type == "table")
            {
                // Identifier can't be bound; name comes from sqlite_master so it is a
                // real table. Quote it to tolerate unusual names.
                std::string quoted = name;
                // escape embedded double-quotes per SQL identifier rules
                for (std::size_t i = quoted.find('"'); i != std::string::npos; i = quoted.find('"', i + 2))
                {
                    quoted.insert(i, 1, '"');
                }
                framelift::SqlStmt count(*store_, ("SELECT COUNT(*) FROM \"" + quoted + "\"").c_str());
                if (count.step() == 1)
                {
                    rowCount = count.integer(0);
                }
            }
            QVariantMap row;
            row.insert(QStringLiteral("name"), QString::fromStdString(name));
            row.insert(QStringLiteral("type"), QString::fromStdString(type));
            row.insert(QStringLiteral("rowCount"), static_cast<qlonglong>(rowCount));
            tables_.push_back(row);
        }
    }
    Q_EMIT tablesChanged();
}

// ── Queries ─────────────────────────────────────────────────────────────────────

void DBViewer::RunSql(const QString& sql, const QString& source)
{
    columns_.clear();
    rows_.clear();
    error_.clear();
    source_ = source;
    truncated_ = false;

    if (!store_)
    {
        error_ = QStringLiteral("Media store unavailable");
        Q_EMIT resultChanged();
        return;
    }

    framelift::SqlStmt stmt(*store_, sql.toUtf8().constData());
    if (!stmt)
    {
        CaptureError();
        if (error_.isEmpty())
        {
            error_ = QStringLiteral("Failed to prepare statement");
        }
        Q_EMIT resultChanged();
        return;
    }

    int rc = stmt.step();
    if (rc < 0)
    {
        CaptureError();
        if (error_.isEmpty())
        {
            error_ = QStringLiteral("Query failed");
        }
        Q_EMIT resultChanged();
        return;
    }

    // Column names are available once the statement has a result set (after step).
    const int columnCount = stmt.count();
    for (int c = 0; c < columnCount; ++c)
    {
        columns_.push_back(QString::fromStdString(stmt.name(c)));
    }

    while (rc == 1)
    {
        QVariantList cells;
        cells.reserve(columnCount);
        for (int c = 0; c < columnCount; ++c)
        {
            cells.push_back(stmt.isNull(c) ? QVariant() : QVariant(QString::fromStdString(stmt.str(c))));
        }
        rows_.push_back(cells);
        if (rows_.size() >= rowCap_)
        {
            truncated_ = stmt.step() == 1; // was there at least one more row?
            break;
        }
        rc = stmt.step();
    }

    Q_EMIT resultChanged();
}

void DBViewer::selectTable(const QString& name)
{
    // Validate against the live table list — identifiers can't be bound.
    bool known = false;
    for (const QVariant& t : std::as_const(tables_))
    {
        if (t.toMap().value(QStringLiteral("name")).toString() == name)
        {
            known = true;
            break;
        }
    }
    if (!known)
    {
        columns_.clear();
        rows_.clear();
        source_ = name;
        truncated_ = false;
        error_ = QStringLiteral("No such table: %1").arg(name);
        Q_EMIT resultChanged();
        return;
    }
    // No LIMIT: RunSql caps at rowCap_ rows (and flags truncation). SQLite fetches
    // rows lazily through Step, so a huge table is not fully scanned.
    QString quoted = name;
    quoted.replace(QStringLiteral("\""), QStringLiteral("\"\""));
    RunSql(QStringLiteral("SELECT * FROM \"%1\"").arg(quoted), name);
}

void DBViewer::runQuery(const QString& sql)
{
    const QString trimmed = sql.trimmed();
    if (trimmed.isEmpty())
    {
        return;
    }
    if (!IsReadOnly(trimmed))
    {
        columns_.clear();
        rows_.clear();
        source_ = trimmed;
        truncated_ = false;
        error_ = QStringLiteral("Only single read-only statements (SELECT / WITH / PRAGMA / EXPLAIN) are allowed");
        Q_EMIT resultChanged();
        return;
    }
    RunSql(trimmed, trimmed);
}

bool DBViewer::IsReadOnly(const QString& sql)
{
    QString s = sql.trimmed();
    if (s.endsWith(QLatin1Char(';')))
    {
        s.chop(1);
        s = s.trimmed();
    }
    if (s.isEmpty() || s.contains(QLatin1Char(';')))
    {
        return false; // enforce a single statement
    }

    // First keyword must be an allow-listed read verb.
    static const QRegularExpression firstWord(QStringLiteral("^\\s*([A-Za-z]+)"));
    const QRegularExpressionMatch m = firstWord.match(s);
    if (!m.hasMatch())
    {
        return false;
    }
    const QString verb = m.captured(1).toUpper();
    if (verb != QLatin1String("SELECT") && verb != QLatin1String("WITH") && verb != QLatin1String("PRAGMA") &&
        verb != QLatin1String("EXPLAIN"))
    {
        return false;
    }

    // Reject any write keyword anywhere (word-boundary, case-insensitive). A WITH/EXPLAIN
    // statement could otherwise smuggle a mutation in a later clause.
    static const QRegularExpression writeWord(
        QStringLiteral("\\b(INSERT|UPDATE|DELETE|DROP|CREATE|ALTER|REPLACE|TRUNCATE|ATTACH|DETACH|VACUUM|REINDEX)\\b"),
        QRegularExpression::CaseInsensitiveOption
    );
    return !writeWord.match(s).hasMatch();
}

// ── Panel state ─────────────────────────────────────────────────────────────────

void DBViewer::togglePanel()
{
    open_ = !open_;
    if (open_)
    {
        ReloadTables(); // reflect writes made since the panel was last open
    }
    else if (ctx_)
    {
        ctx_->Publish<PanelLayoutEvent>({1, 0.f});
    }
    Q_EMIT panelStateChanged();
}

void DBViewer::publishVisibleWidth(const qreal width)
{
    if (ctx_)
    {
        ctx_->Publish<PanelLayoutEvent>({1, static_cast<float>(width)});
    }
}

void DBViewer::refresh()
{
    ReloadTables();
}
