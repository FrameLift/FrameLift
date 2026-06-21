#include "History.h"
#include <cstddef>
#include <cstdio>
#include <string>
#include <utility>

#include "Version.h"
#include <cstring>
#include <framelift/core.h>
#include <framelift/ui.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <framelift/JsonHelpers.h>
#include <iterator>
#include <numeric>
#include <thread>
#include <vector>

// ── Helpers ───────────────────────────────────────────────────────────────────

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

// ── Constructor ───────────────────────────────────────────────────────────────

History::History() : Panel(Side::Right, 320.f, "History")
{
}

// ── ModuleBase hooks ───────────────────────────────────────────────────────

std::vector<framelift::SettingsField> History::SettingsFields()
{
    return {{"maxEntries", &maxEntries_, 200}};
}

std::vector<framelift::Keybind> History::Keybinds()
{
    return {
        {"Toggle history", "toggleHistory", &toggleHistoryKey_, "H", [this]
        {
            Toggle();
        }}
    };
}

void History::OnInstall(IModuleContext& ctx)
{
    SetupSettingsPage(ctx);

    SetContext(&ctx);
    SetFocusManager(ctx.GetService<FocusManager>(), this);

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
                        Toggle();
                    }
                );
            }
        );
    }
}

void History::RenderSettings(UIContext& ctx)
{
    Widgets::SectionHeader(ctx, "Recent files");
    Widgets::SliderInt(ctx, "Max history entries", "Maximum number of recent files to remember.", maxEntries_, 10, 500);
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

    if (kp.key == Keys::Up)
    {
        CursorUp();
        return true;
    }
    if (kp.key == Keys::Down)
    {
        CursorDown();
        return true;
    }
    if (kp.key == Keys::Return)
    {
        ConfirmCursor();
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
    cursor_ = -1;
    searchQuery_.clear();
    filteredIndices_.clear();
    Save();
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

    if (filteredIndices_.empty())
    {
        cursor_ = -1;
    }
    else if (cursor_ >= static_cast<int>(filteredIndices_.size()))
    {
        cursor_ = static_cast<int>(filteredIndices_.size()) - 1;
    }
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

// ── Keyboard navigation ───────────────────────────────────────────────────────

void History::CursorUp()
{
    if (filteredIndices_.empty())
    {
        return;
    }

    if (cursor_ - 1 >= 0)
    {
        cursor_ -= 1;
    }
}

void History::CursorDown()
{
    if (filteredIndices_.empty())
    {
        return;
    }

    const int n = static_cast<int>(filteredIndices_.size());
    if (cursor_ + 1 < n)
    {
        cursor_ += 1;
    }
}

void History::ConfirmCursor() const
{
    if (cursor_ >= 0 && cursor_ < static_cast<int>(filteredIndices_.size()))
    {
        ctx_->Publish<OpenFileRequestEvent>({entries_[filteredIndices_[cursor_]].path.c_str()});
    }
}

// ── Panel content ─────────────────────────────────────────────────────────────

void History::RenderContent(const float panelW, float /*panelH*/, UIContext& ctx)
{
    constexpr float rowH = 58.f;
    constexpr float padding = 12.f;
    constexpr float headerH = 36.f;
    constexpr float searchH = 32.f;
    constexpr float popReserve = 26.f; // right-edge space reserved for the Panel pop-out toggle

    // ── Header ───────────────────────────────────────────────────────────────
    {
        const UI::Vec2 hdrMin = ctx.GetCursorScreenPos();
        const UI::Vec2 hdrMax = {hdrMin.x + panelW, hdrMin.y + headerH};
        auto& dl = ctx.GetWindowDrawList();

        dl.AddRectFilled(hdrMin, hdrMax, UI::MakeColor32(18, 10, 28, 230));

        // Title – top-left (suppressed when popped out: the OS title bar shows it)
        float counterX = padding;
        if (!IsPoppedOut())
        {
            ctx.SetCursorPosY(10.f);
            ctx.SetCursorPosX(padding);
            ctx.TextColored(UI::Color4f(0.88f, 0.82f, 1.f, 1.f), "History");
            counterX = padding + 60.f;
        }

        // Entry count – beside the title
        if (!entries_.empty())
        {
            ctx.SetCursorPosY(10.f);
            ctx.SetCursorPosX(counterX);
            const std::string counter = searchQuery_.empty()
                                            ? std::to_string(entries_.size())
                                            : std::to_string(filteredIndices_.size()) + " / " +
                                              std::to_string(entries_.size());
            ctx.TextColored(UI::Color4f(0.5f, 0.45f, 0.65f, 1.f), counter.c_str());
        }

        // ─ Clear button (X) ──────────────────────────────────────────────────
        ctx.SetCursorPosY(8.f);
        ctx.SetCursorPosX(panelW - popReserve - padding - 22.f);
        ctx.PushStyleColor(UI::ColorSlot::Button, UI::Color4f(0.15f, 0.10f, 0.25f, 0.70f));
        ctx.PushStyleColor(UI::ColorSlot::ButtonHovered, UI::Color4f(0.45f, 0.15f, 0.20f, 0.85f));
        if (ctx.Button("X", {22.f, 20.f}))
        {
            Clear();
        }
        ctx.PopStyleColor(2);

        // Bottom separator
        dl.AddLine(
            {hdrMin.x + padding, hdrMax.y - 1.f}, {hdrMin.x + panelW - padding, hdrMax.y - 1.f},
            UI::MakeColor32(80, 55, 120, 200)
        );

        ctx.SetCursorPosY(headerH);
    }

    // ── Search ────────────────────────────────────────────────────────────────
    {
        ctx.SetCursorPosY(headerH + 6.f);
        ctx.SetCursorPosX(padding);
        ctx.SetNextItemWidth(panelW - padding * 2.f);
        if (ctx.InputTextWithHint("##search", "Search...", searchQuery_))
        {
            RebuildFilter();
            cursor_ = filteredIndices_.empty() ? -1 : 0;
        }
        ctx.SetCursorPosY(headerH + searchH);
    }

    ctx.BeginChild("##histItems", UI::Vec2(panelW, 0.f));

    for (int i = 0; i < static_cast<int>(filteredIndices_.size()); ++i)
    {
        const int ei = filteredIndices_[i];
        const bool isCursor = i == cursor_;

        // Content-local Y of this row's top. Scroll-independent: ImGui applies the
        // scroll offset internally. Using window-local coordinates (not screen-derived
        // ones) keeps the content height stable while scrolling so ScrollY isn't clamped.
        const float rowTop = static_cast<float>(i) * rowH;
        ctx.SetCursorPosY(rowTop);

        const UI::Vec2 rowMin = ctx.GetCursorScreenPos();
        const UI::Vec2 rowMax = {rowMin.x + panelW, rowMin.y + rowH};
        auto& dl = ctx.GetWindowDrawList();

        if (isCursor)
        {
            dl.AddRectFilled(rowMin, rowMax, UI::MakeColor32(60, 45, 90, 160));
        }

        ctx.PushID(i);
        ctx.SetCursorPosX(0.f);
        if (ctx.Selectable("##row", isCursor, UI::SelectableFlags::None, UI::Vec2(panelW, rowH - 2.f)))
        {
            cursor_ = i;
            ConfirmCursor();
        }
        ctx.PopID();

        ctx.SetCursorPosX(padding);
        ctx.SetCursorPosY(rowTop + 6.f);

        const UI::Color4f nameCol = isCursor ? UI::Color4f(1.f, 1.f, 1.f, 1.f) : UI::Color4f(0.82f, 0.78f, 0.9f, 1.f);
        ctx.TextColored(nameCol, entries_[ei].label.c_str());

        ctx.SetCursorPosX(padding);
        ctx.SetCursorPosY(rowTop + 24.f);
        ctx.TextColored(UI::Color4f(0.45f, 0.42f, 0.55f, 1.f), entries_[ei].dir.c_str());

        ctx.SetCursorPosX(padding);
        ctx.SetCursorPosY(rowTop + 40.f);
        ctx.TextColored(UI::Color4f(0.35f, 0.32f, 0.45f, 1.f), entries_[ei].meta.c_str());

        dl.AddLine(
            {rowMin.x + padding, rowMax.y - 1.f}, {rowMax.x - padding, rowMax.y - 1.f}, UI::MakeColor32(70, 55, 100, 80)
        );
    }

    if (filteredIndices_.empty())
    {
        ctx.SetCursorPosY(40.f);
        ctx.SetCursorPosX(padding);
        const char* msg = entries_.empty() ? "No history yet." : "No results.";
        ctx.TextColored(UI::Color4f(0.4f, 0.35f, 0.55f, 1.f), msg);
    }

    ctx.EndChild();
}
