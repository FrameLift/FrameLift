#include "History.h"
#include "HistorySettings.h"
#include <cstddef>
#include <cstdio>
#include <string>
#include <utility>

#include "Version.h"
#include <cstring>
#include <framelift/core.h>

#include <QtCore/QVariantMap>
#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <framelift/MediaStoreHelpers.h>
#include <numeric>
#include <vector>

// ── Helpers ───────────────────────────────────────────────────────────────────

History::History()
{
    // Invalidate the QmlEntries cache on every change to the entries projection.
    QObject::connect(
        this, &History::historyChanged, this,
        [this]
        {
            entriesCacheDirty_ = true;
        }
    );
}

History::~History() = default;

namespace
{
// Local "%Y-%m-%d %H:%M:%S" display string for a unix timestamp.
std::string FormatDate(std::time_t t)
{
    tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t); // MSVC: (struct tm*, const time_t*)
#else
    localtime_r(&t, &tm); // POSIX: (const time_t*, struct tm*)
#endif
    char dateBuf[20];
    std::strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d %H:%M:%S", &tm);
    return dateBuf;
}
} // namespace

std::string History::FilenameOf(const std::string& path)
{
    try
    {
        return std::filesystem::path(path).filename().string();
    }
    catch (...)
    {
        return path;
    }
}

void History::FormatEntry(Entry& e)
{
    try
    {
        e.dir = std::filesystem::path(e.path).parent_path().string();
    }
    catch (...)
    {
        e.dir.clear();
    }

    const int total = static_cast<int>(e.resumePos);
    const int h = total / 3600, m = (total % 3600) / 60, s = total % 60;
    char posBuf[16];
    if (h > 0)
    {
        std::snprintf(posBuf, sizeof(posBuf), "%d:%02d:%02d", h, m, s);
    }
    else
    {
        std::snprintf(posBuf, sizeof(posBuf), "%d:%02d", m, s);
    }
    e.meta = e.playbackDate + "  \xc2\xb7  " + posBuf;
}

// ── ModuleBase hooks ───────────────────────────────────────────────────────

std::vector<framelift::Keybind> History::Keybinds()
{
    return {
        {"Toggle history", "toggleHistory", &toggleHistoryKey_, "H", [this]
         {
             togglePanel();
         }}
    };
}

void History::LoadSettings(IModuleSettings& ps)
{
    maxEntries_ = ps.GetInt("maxEntries", 200);
}

void History::SaveSettings(IModuleSettings& ps)
{
    ps.SetInt("maxEntries", maxEntries_);
}

void History::OnInstall(IModuleContext& ctx)
{
    // Discover the shared media store; without it history stays in-memory only.
    if (!store_)
    {
        SetMediaStore(ctx.GetService<IMediaStore>());
    }

    if (auto* pages = ctx.GetService<ISettingsPageRegistry>())
    {
        settingsPage_ = std::make_unique<HistorySettings>(*this);
        pages->RegisterSettingsPage(
            "history", "History", "qrc:/qt/qml/FrameLift/Plugins/History/HistorySettings.qml", settingsPage_.get(), 310
        );
    }

    framelift::Subscribe<FileOpenedEvent>(
        ctx,
        [this](const FileOpenedEvent& e)
        {
            AddEntry(e.path);
        }
    );
    framelift::Subscribe<FileEndedEvent>(
        ctx,
        [this](const FileEndedEvent& e)
        {
            UpdateResumePos(e.path, e.position);
        }
    );

    ctx.RegisterService<IHistory>(this);

    if (auto* menu = ctx.GetService<ContextMenu>())
    {
        framelift::AddSection(
            *menu,
            [this](ContextMenu& m)
            {
                m.AddSeparator();
                framelift::AddItem(
                    m, "History", "toggleHistory",
                    [this]
                    {
                        togglePanel();
                    }
                );
            }
        );
    }
}

void History::ApplySettings(int maxEntries)
{
    maxEntries_ = maxEntries;
    while (static_cast<int>(entries_.size()) > MaxEntries())
    {
        entries_.pop_back();
    }
    RebuildFilter();
    if (auto* store = ctx_ ? ctx_->GetService<ISettingsStore>() : nullptr)
    {
        IModuleSettings& ps = store->GetModuleSettings(SettingsSection().c_str());
        SaveSettings(ps);
        ps.Save();
    }
    PersistTrim();
    Q_EMIT historyChanged();
}

// ── IModule ──────────────────────────────────────────────────────────────────

bool History::HandleKeyDownEvent(const AppEvent& e)
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

    // Navigation and activation are handled natively by the focused QML
    // ListView (Qt's built-in Up/Down/Return with scroll-to-current). We only
    // swallow these keys here so they don't also fire global player hotkeys
    // while the panel is open; the key still reaches the focused QML item
    // because the host event filter never consumes it.
    if (kp.key == Keys::Up || kp.key == Keys::Down || kp.key == Keys::Return)
    {
        return true;
    }
    return false;
}

int History::MaxEntries() const
{
    return maxEntries_;
}

void History::SetMediaStore(IMediaStore* store)
{
    store_ = store;
    if (!store_)
    {
        return;
    }
    EnsureSchema();
    Load();
}

// ── Persistence ───────────────────────────────────────────────────────────────

void History::EnsureSchema()
{
    if (store_->GetSchemaVersion("history") >= 1)
    {
        return;
    }
    (void)store_->Exec(
        "CREATE TABLE IF NOT EXISTS history_entries ("
        "    path            TEXT PRIMARY KEY,"
        "    resume_pos      REAL    NOT NULL DEFAULT 0,"
        "    last_played_utc INTEGER NOT NULL,"
        "    play_count      INTEGER NOT NULL DEFAULT 1)"
    );
    (void)store_->Exec(
        "CREATE INDEX IF NOT EXISTS history_entries_by_last_played "
        "ON history_entries(last_played_utc DESC)"
    );
    (void)store_->SetSchemaVersion("history", 1);
}

void History::Load()
{
    // last_played_utc has second resolution, so rowid breaks ties by insertion order
    // (re-adds within the same second keep their original rowid — acceptable skew).
    framelift::SqlStmt select(
        *store_, "SELECT path, resume_pos, last_played_utc FROM history_entries "
                 "ORDER BY last_played_utc DESC, rowid DESC LIMIT ?"
    );
    (void)select.bind(0, static_cast<long long>(MaxEntries()));
    while (select.step() == 1)
    {
        std::string path = select.str(0);
        if (path.empty())
        {
            continue;
        }
        const double pos = select.num(1);
        std::string date = FormatDate(static_cast<std::time_t>(select.integer(2)));
        std::string label = FilenameOf(path);
        Entry e{std::move(path), std::move(label), pos, std::move(date)};
        FormatEntry(e);
        entries_.push_back(std::move(e));
    }
    RebuildFilter();
}

void History::PersistEntry(const char* path, const double resumePos, const long long playedUtc) const
{
    if (!store_)
    {
        return;
    }
    // Re-adding a file bumps recency and play count but keeps the stored resume
    // position (mirrors the in-memory dedup that carries existingPos over).
    framelift::SqlStmt upsert(
        *store_, "INSERT INTO history_entries(path, resume_pos, last_played_utc) VALUES(?, ?, ?) "
                 "ON CONFLICT(path) DO UPDATE SET last_played_utc = excluded.last_played_utc, "
                 "play_count = play_count + 1"
    );
    (void)upsert.bind(0, path);
    (void)upsert.bind(1, resumePos);
    (void)upsert.bind(2, playedUtc);
    (void)upsert.step();
    PersistTrim();
}

void History::PersistResumePos(const char* path, const double pos) const
{
    if (!store_)
    {
        return;
    }
    framelift::SqlStmt update(*store_, "UPDATE history_entries SET resume_pos = ? WHERE path = ?");
    (void)update.bind(0, pos);
    (void)update.bind(1, path);
    (void)update.step();
}

void History::PersistTrim() const
{
    if (!store_)
    {
        return;
    }
    framelift::SqlStmt trim(
        *store_, "DELETE FROM history_entries WHERE path NOT IN "
                 "(SELECT path FROM history_entries ORDER BY last_played_utc DESC, rowid DESC LIMIT ?)"
    );
    (void)trim.bind(0, static_cast<long long>(MaxEntries()));
    (void)trim.step();
}

void History::PersistClear() const
{
    if (store_)
    {
        (void)store_->Exec("DELETE FROM history_entries");
    }
}

void History::Clear()
{
    entries_.clear();
    searchQuery_.clear();
    filteredIndices_.clear();
    PersistClear();
    Q_EMIT historyChanged();
}

void History::RebuildFilter()
{
    filteredIndices_.clear();

    if (searchQuery_.empty())
    {
        filteredIndices_.resize(entries_.size());
        std::iota(filteredIndices_.begin(), filteredIndices_.end(), 0);
    }
    else
    {
        std::string query = searchQuery_;
        std::ranges::transform(
            query, query.begin(),
            [](const unsigned char c)
            {
                return std::tolower(c);
            }
        );

        for (int i = 0; i < static_cast<int>(entries_.size()); ++i)
        {
            auto containsQuery = [&](const std::string& s)
            {
                std::string lower = s;
                std::ranges::transform(
                    lower, lower.begin(),
                    [](const unsigned char c)
                    {
                        return std::tolower(c);
                    }
                );
                return lower.find(query) != std::string::npos;
            };

            if (containsQuery(entries_[i].label) || containsQuery(entries_[i].path))
            {
                filteredIndices_.push_back(i);
            }
        }
    }

    Q_EMIT historyChanged();
}

// ── Entry management ──────────────────────────────────────────────────────────

int History::GetMostRecent(char* buf, int cap) const noexcept
{
    const std::string& p = entries_.empty() ? std::string{} : entries_.front().path;
    const int len = static_cast<int>(p.size());
    if (buf && cap > 0)
    {
        const int n = len < cap - 1 ? len : cap - 1;
        std::memcpy(buf, p.c_str(), static_cast<std::size_t>(n));
        buf[n] = '\0';
    }
    return len;
}

void History::AddEntry(const char* path) noexcept
{
    if (!path)
    {
        return; // FileOpenedEvent::path defaults to null; guard like UpdateResumePos/GetResumePos.
    }
    // Preserve any existing resume position before removing the duplicate.
    double existingPos = 0.0;
    for (const auto& e : entries_)
    {
        if (e.path != path)
        {
            continue;
        }
        existingPos = e.resumePos;
        break;
    }

    std::erase_if(
        entries_,
        [&](const Entry& e)
        {
            return e.path == path;
        }
    );

    const std::time_t now = std::time(nullptr);
    Entry e{path, FilenameOf(path), existingPos, FormatDate(now)};
    FormatEntry(e);
    entries_.push_front(std::move(e));

    if (static_cast<int>(entries_.size()) > MaxEntries())
    {
        entries_.pop_back();
    }

    RebuildFilter();
    PersistEntry(path, existingPos, static_cast<long long>(now));
    Q_EMIT historyChanged();
}

void History::UpdateResumePos(const char* path, const double pos) noexcept
{
    if (!path)
    {
        return;
    }
    for (auto& e : entries_)
    {
        if (e.path == path)
        {
            e.resumePos = pos;
            FormatEntry(e); // refresh cached meta string with the new position
            PersistResumePos(path, pos);
            Q_EMIT historyChanged();
            return;
        }
    }
}

double History::GetResumePos(const char* path) const noexcept
{
    if (!path)
    {
        return 0.0;
    }
    for (const auto& e : entries_)
    {
        if (e.path == path)
        {
            return e.resumePos;
        }
    }
    return 0.0;
}

void History::SetSearch(const QString& value)
{
    const std::string next = value.toStdString();
    if (next == searchQuery_)
    {
        return;
    }
    searchQuery_ = next;
    RebuildFilter();
}

QVariantList History::QmlEntries() const
{
    if (!entriesCacheDirty_)
    {
        return entriesCache_;
    }
    QVariantList result;
    result.reserve(static_cast<qsizetype>(filteredIndices_.size()));
    for (int i = 0; i < static_cast<int>(filteredIndices_.size()); ++i)
    {
        const Entry& entry = entries_[filteredIndices_[i]];
        QVariantMap row;
        row.insert(QStringLiteral("label"), QString::fromStdString(entry.label));
        row.insert(QStringLiteral("directory"), QString::fromStdString(entry.dir));
        row.insert(QStringLiteral("meta"), QString::fromStdString(entry.meta));
        result.push_back(row);
    }
    entriesCache_ = std::move(result);
    entriesCacheDirty_ = false;
    return entriesCache_;
}

void History::togglePanel()
{
    SetOpen(!open_);
    Q_EMIT panelStateChanged();
}

void History::activateIndex(const int filteredIndex)
{
    if (filteredIndex < 0 || filteredIndex >= static_cast<int>(filteredIndices_.size()))
    {
        return;
    }
    if (ctx_)
    {
        ctx_->Publish<OpenFileRequestEvent>({entries_[filteredIndices_[filteredIndex]].path.c_str()});
    }
}

void History::publishVisibleWidth(const qreal width)
{
    if (ctx_)
    {
        ctx_->Publish<PanelLayoutEvent>({1, static_cast<float>(width)});
    }
}

void History::SetOpen(const bool value)
{
    if (open_ == value)
    {
        return;
    }
    open_ = value;
    if (!open_)
    {
        if (ctx_)
        {
            ctx_->Publish<PanelLayoutEvent>({1, 0.f});
        }
    }
    Q_EMIT historyChanged();
}
