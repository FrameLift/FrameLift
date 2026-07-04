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
#include <atomic>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <framelift/JsonHelpers.h>
#include <fstream>
#include <iterator>
#include <numeric>
#include <thread>
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
    // Discover the host JSON service before SetStoragePath so Load() can read.
    json_ = ctx.GetService<IJson>();

    // Register storage path from pref dir if not already set by App.
    if (storagePath_.empty())
    {
        const std::string prefPath = framelift::GetPrefPath(ctx);
        if (!prefPath.empty())
        {
            SetStoragePath(prefPath + "history.json");
        }
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
            Save();
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
    Save();
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

void History::SetStoragePath(std::string path)
{
    storagePath_ = std::move(path);
    Load();
}

// ── Persistence ───────────────────────────────────────────────────────────────

void History::Load()
{
    if (!json_)
    {
        return;
    }

    std::ifstream f(storagePath_, std::ios::binary);
    if (!f)
    {
        return;
    }
    const std::string contents((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    const framelift::JsonDocument doc(*json_, contents);
    const framelift::JsonRef root = doc.root();
    if (doc && root.isArray())
    {
        for (int i = 0; i < root.size(); ++i)
        {
            const framelift::JsonRef item = root.at(i);
            std::string path = item.str("p");
            if (path.empty())
            {
                continue;
            }
            const double pos = item.num("r", 0.0);
            std::string date = item.str("d");
            std::string label = FilenameOf(path);
            Entry e{std::move(path), std::move(label), pos, std::move(date)};
            FormatEntry(e);
            entries_.push_back(std::move(e));
        }
    }
    RebuildFilter();
}

void History::Clear()
{
    entries_.clear();
    searchQuery_.clear();
    filteredIndices_.clear();
    Save();
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

void History::Save() const noexcept
{
    if (storagePath_.empty() || !json_)
    {
        return;
    }

    // Serialise synchronously on the caller's thread (which already owns entries_),
    // so the detached writer only does file IO and never touches the JSON service —
    // no cross-thread service use and no json_ lifetime hazard on the writer.
    framelift::JsonWriter arr = framelift::JsonWriter::Array(*json_);
    for (const auto& e : entries_)
    {
        framelift::JsonWriter o = framelift::JsonWriter::Object(*json_);
        o.set("p", e.path);
        o.set("r", e.resumePos);
        o.set("d", e.playbackDate);
        arr.append(std::move(o));
    }
    std::string payload = arr.dump(2);

    const std::string dst = storagePath_;
    // Monotonic stamp so the writer can tell newer snapshots from older ones, and
    // a unique suffix so two concurrent saves never clobber the same temp file.
    const unsigned seq = saveSeq_.fetch_add(1, std::memory_order_relaxed);
    auto coord = saveCoord_;

    std::thread(
        [payload = std::move(payload), dst, seq, coord]
        {
            std::error_code ec;
            std::filesystem::create_directories(std::filesystem::path(dst).parent_path(), ec);

            // Write to a unique temp file, then atomically rename over the target so
            // a crash mid-write or a racing save can never leave a truncated file.
            const std::string tmp = dst + "." + std::to_string(seq) + ".tmp";
            {
                std::ofstream f(tmp, std::ios::trunc);
                f << payload << '\n';
            }

            // Commit under the lock so renames are serialised and ordered: drop this
            // snapshot if a newer save has already been published, otherwise rename
            // it into place and become the newest. Guarantees the latest entry wins
            // regardless of the order the detached writer threads are scheduled.
            std::lock_guard lock(coord->mutex);
            if (coord->any && seq < coord->published)
            {
                std::filesystem::remove(tmp, ec);
                return;
            }
            std::filesystem::rename(tmp, dst, ec);
            if (ec)
            {
                std::filesystem::remove(tmp, ec);
                return;
            }
            coord->published = seq;
            coord->any = true;
        }
    ).detach();
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
    tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now); // MSVC: (struct tm*, const time_t*)
#else
    localtime_r(&now, &tm); // POSIX: (const time_t*, struct tm*)
#endif
    char dateBuf[20];
    std::strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d %H:%M:%S", &tm);

    Entry e{path, FilenameOf(path), existingPos, dateBuf};
    FormatEntry(e);
    entries_.push_front(std::move(e));

    if (static_cast<int>(entries_.size()) > MaxEntries())
    {
        entries_.pop_back();
    }

    RebuildFilter();
    Save();
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
