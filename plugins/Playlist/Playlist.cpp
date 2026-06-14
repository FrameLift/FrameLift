#include "Playlist.h"
#include "ExtensionFilter.h"
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "Version.h"
#include <framelift/core.h>
#include <framelift/platform.h>
#include <framelift/ui.h>

#include <algorithm>
#include <filesystem>
#include <numeric>
#include <random>

// Read a string setting via the ABI-stable per-key getter (host owns the value).
// Falls back to the supplied default if no context is available.
static std::string ReadStringSetting(IPluginContext* ctx, const char* key, const char* fallback)
{
    if (!ctx)
    {
        return fallback;
    }
    const int n = ctx->GetSettingString(key, nullptr, 0);
    std::string s(static_cast<std::size_t>(n), '\0');
    if (n > 0)
    {
        ctx->GetSettingString(key, s.data(), n + 1);
    }
    return s;
}

// ── File scanner ──────────────────────────────────────────────────────────────

static bool IsVideoFile(const std::filesystem::path& p, const std::string& extensions)
{
    return ExtensionInList(p, extensions);
}

static void ScanVideos(
    const std::filesystem::path& dir, const int depth, const int maxDepth, const std::string& extensions,
    std::vector<std::string>& out
)
{
    if (depth > maxDepth)
    {
        return;
    }
    std::error_code ec;
    for (auto& entry : std::filesystem::directory_iterator(dir, ec))
    {
        if (entry.is_regular_file(ec) && IsVideoFile(entry.path(), extensions))
        {
            out.push_back(entry.path().string());
        }
        else if (entry.is_directory(ec))
        {
            ScanVideos(entry.path(), depth + 1, maxDepth, extensions, out);
        }
    }
}

// ── Helpers ───────────────────────────────────────────────────────────────────
Playlist::~Playlist()
{
    if (auto* dw = ctx_ ? ctx_->GetService<IDirWatcher>() : nullptr)
    {
        dw->Unwatch();
    }
}

std::string Playlist::FilenameOf(const std::string& path)
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

// ── PluginBase hooks ───────────────────────────────────────────────────────

std::vector<framelift::SettingsField> Playlist::SettingsFields()
{
    return {
        {"scanSubdirs", &scanSubdirs_, true},
        {"scanMaxDepth", &scanMaxDepth_, 5},
        {"mixedPlaylist", &mixedPlaylist_, false},
        {"imageSlideshow", &imageSlideshow_, false},
        {"slideshowDuration", &slideshowDuration_, 5.0f},
        {"autoReload", &autoReload_, true}
    };
}

std::vector<framelift::Keybind> Playlist::Keybinds()
{
    return {
        {"Toggle playlist", "togglePlaylist", &togglePlaylistKey_, "L",
         [this]
         {
             Toggle();
         }},
        {"Next track", "nextTrack", &nextTrackKey_, "Ctrl+Right",
         [this]
         {
             Next();
             if (ctx_)
             {
                 ctx_->Publish<NotificationEvent>({"Next"});
             }
         }},
        {"Previous track", "prevTrack", &prevTrackKey_, "Ctrl+Left",
         [this]
         {
             Prev();
             if (ctx_)
             {
                 ctx_->Publish<NotificationEvent>({"Previous"});
             }
         }},
        {"Reload playlist", "reloadPlaylist", &reloadPlaylistKey_, "Ctrl+R",
         [this]
         {
             Reload();
         }},
        {"Toggle shuffle", "toggleShuffle", &toggleShuffleKey_, "Shift+S", [this]
        {
            ToggleShuffle();
        }}
    };
}

void Playlist::OnInstall(IPluginContext& ctx)
{
    if (auto* w = ctx.GetService<IAppWindow>())
    {
        dirChangedEventType_ = w->RegisterCustomEventType();
    }

    SetContext(&ctx);
    SetFocusManager(ctx.GetService<FocusManager>(), this);

    framelift::Subscribe<OpenFileRequestEvent>(
        ctx,
        [this](const OpenFileRequestEvent& e)
        {
            // Remote URLs (http://, flsec://, ...) are owned by the RemoteStream
            // plugin. Ignore them here so we neither reject them as missing files
            // nor try to scan a directory for a non-filesystem path.
            if (framelift::IsRemoteUrl(e.path))
            {
                return;
            }

            if (e.path && e.path[0])
            {
                std::error_code ec;
                if (!std::filesystem::exists(e.path, ec))
                {
                    if (ctx_)
                    {
                        ctx_->Publish<NotificationEvent>({"File not found"});
                    }
                    return;
                }
            }

            if (e.rebuildPlaylist)
            {
                OpenFile(e.path);
            }
            else
            {
                LoadFile(e.path);
            }
        }
    );

    SetupSettingsPage(ctx);

    if (auto* menu = ctx.GetService<ContextMenu>())
    {
        framelift::AddSection(
            *menu,
            [this](ContextMenu& m)
            {
                m.AddSeparator();
                framelift::AddItem(
                    m, "Playlist", "togglePlaylist",
                    [this]
                    {
                        Toggle();
                    }
                );
            }
        );
    }
}

void Playlist::RenderSettings(UIContext& ctx)
{
    RenderSettingsContent(ctx);
}

// ── IPlugin ──────────────────────────────────────────────────────────────────

bool Playlist::HandleEvent(const AppEvent& e)
{
    if (e.type == AppEventType::Quit)
    {
        FlushCurrentPos();
        return false; // don't consume — let other plugins see Quit too
    }
    if (e.type == AppEventType::Custom && dirChangedEventType_ != 0)
    {
        if (e.AsCustom().eventType == dirChangedEventType_)
        {
            Reload();
            return false;
        }
    }
    return PluginBase::HandleEvent(e);
}

bool Playlist::HandleKeyDownEvent(const AppEvent& e)
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

void Playlist::FlushCurrentPos() const
{
    if (!currentFile_.empty() && currentTimePos_ > 5.0 && ctx_)
    {
        ctx_->Publish<FileEndedEvent>({currentFile_.c_str(), currentTimePos_});
    }
}

void Playlist::HandleMediaEvent(const MediaEvent& e)
{
    if (e.type == MediaEventType::PropertyChange && e.property.prop == PlayerProperty::TimePos &&
        e.property.type == PropertyType::Double)
    {
        const double v = e.property.value.dbl;
        currentTimePos_ = v >= 0.0 ? v : 0.0;
        return;
    }

    if (e.type != MediaEventType::EndFile || e.endReason != EndFileReason::Eof)
    {
        return;
    }

    if (!currentFile_.empty() && ctx_)
    {
        ctx_->Publish<FileEndedEvent>({currentFile_.c_str(), 0.0});
    }

    if (Current() < Count() - 1)
    {
        Next();
    }
}

// ── File loading ──────────────────────────────────────────────────────────────

void Playlist::LoadFile(const char* path) noexcept
{
    if (!path || !path[0])
    {
        return;
    }
    const std::string pathStr(path);

    if (!currentFile_.empty() && currentTimePos_ > 5.0 && ctx_)
    {
        ctx_->Publish<FileEndedEvent>({currentFile_.c_str(), currentTimePos_});
    }

    currentFile_ = pathStr;
    currentTimePos_ = 0.0;

    auto* player = ctx_ ? ctx_->GetService<IMediaPlayer>() : nullptr;
    if (player)
    {
        const std::string imageExt = ReadStringSetting(ctx_, "files.imageExtensions", "png;jpg;jpeg;gif;bmp;webp");
        if (IsVideoFile(std::filesystem::path(pathStr), imageExt))
        {
            const double dur = imageSlideshow_ ? static_cast<double>(slideshowDuration_) : 0.0;
            player->SetImageDisplayDuration(dur);
        }
    }

    auto* hist = ctx_ ? ctx_->GetService<IHistory>() : nullptr;
    const double resumePos = hist ? hist->GetResumePos(path) : 0.0;
    if (player)
    {
        player->LoadFile(path, resumePos > 5.0 ? resumePos : 0.0);
        player->SetPause(false);
    }

    if (auto* w = ctx_ ? ctx_->GetService<IAppWindow>() : nullptr)
    {
        const std::string titleStr = "FrameLift \xe2\x80\x94 " + FilenameOf(pathStr);
        w->SetTitle(titleStr.c_str());
    }

    if (ctx_)
    {
        ctx_->Publish<FileOpenedEvent>({path});
    }
}

void Playlist::OpenFile(const char* path) noexcept
{
    if (!path || !path[0])
    {
        return;
    }

    const auto dir = std::filesystem::path(path).parent_path();
    const std::string videoExt =
        ReadStringSetting(ctx_, "files.videoExtensions", "mp4;mkv;avi;mov;wmv;flv;webm;m4v;mpg;mpeg");
    const std::string imageExt = ReadStringSetting(ctx_, "files.imageExtensions", "png;jpg;jpeg;gif;bmp;webp");
    const int maxDepth = scanSubdirs_ ? scanMaxDepth_ : 0;

    std::string scanExt;
    if (mixedPlaylist_)
    {
        scanExt = videoExt + ";" + imageExt;
    }
    else if (IsVideoFile(std::filesystem::path(path), imageExt))
    {
        scanExt = imageExt;
    }
    else
    {
        scanExt = videoExt;
    }

    std::vector<std::string> files;
    ScanVideos(dir, 0, maxDepth, scanExt, files);
    std::ranges::sort(files);

    Clear();

    const bool hasPath = std::ranges::find(files, path) != files.end();
    entries_.reserve(files.size() + (hasPath ? 0 : 1));

    for (auto& f : files)
    {
        AddFile(std::move(f));
    }

    if (!hasPath)
    {
        AddFile(path);
    }

    // Start watching the directory for new/removed files.
    watchedDir_ = dir.string();
    auto* dw = ctx_ ? ctx_->GetService<IDirWatcher>() : nullptr;
    auto* appWin = ctx_ ? ctx_->GetService<IAppWindow>() : nullptr;
    if (dw && appWin)
    {
        struct Ctx
        {
            Playlist* self;
            IAppWindow* win;
        };

        static const auto cb = [](void* ud)
        {
            auto* c = static_cast<Ctx*>(ud);
            if (c->self->autoReload_)
            {
                c->win->PushCustomEvent(c->self->dirChangedEventType_);
            }
        };
        watchCbCtx_ = {this, appWin};
        dw->Watch(watchedDir_.c_str(), cb, &watchCbCtx_, maxDepth);
    }

    ActivateByPath(path);
}

// ── Entry management ──────────────────────────────────────────────────────────

void Playlist::AddFile(std::string path)
{
    auto label = FilenameOf(path);
    entries_.emplace_back(std::move(path), std::move(label));
}

void Playlist::AddFiles(const std::vector<std::string>& paths)
{
    entries_.reserve(entries_.size() + paths.size());
    for (auto& p : paths)
    {
        AddFile(p);
    }
}

void Playlist::Clear()
{
    entries_.clear();
    current_ = -1;
    cursor_ = -1;
}

void Playlist::Activate(const int index)
{
    if (index < 0 || index >= static_cast<int>(entries_.size()))
    {
        return;
    }
    current_ = index;
    LoadFile(entries_[current_].path.c_str());
}

void Playlist::Next()
{
    if (entries_.empty())
    {
        return;
    }
    Activate((current_ + 1) % static_cast<int>(entries_.size()));
}

void Playlist::Prev()
{
    if (entries_.empty())
    {
        return;
    }
    const int n = static_cast<int>(entries_.size());
    Activate((current_ - 1 + n) % n);
}

void Playlist::ActivateLast()
{
    if (!entries_.empty())
    {
        Activate(static_cast<int>(entries_.size()) - 1);
    }
}

void Playlist::ActivateByPath(const std::string& path)
{
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i)
    {
        if (entries_[i].path == path)
        {
            Activate(i);
            return;
        }
    }
}

// ── Reload / Shuffle ───────────────────────────────────────────────────────────

void Playlist::Reload()
{
    if (watchedDir_.empty())
    {
        return;
    }

    const std::string currentPath =
        current_ >= 0 && current_ < static_cast<int>(entries_.size()) ? entries_[current_].path : std::string{};

    const std::string videoExt =
        ReadStringSetting(ctx_, "files.videoExtensions", "mp4;mkv;avi;mov;wmv;flv;webm;m4v;mpg;mpeg");
    const std::string imageExt = ReadStringSetting(ctx_, "files.imageExtensions", "png;jpg;jpeg;gif;bmp;webp");
    const int maxDepth = scanSubdirs_ ? scanMaxDepth_ : 0;

    std::string scanExt;
    if (mixedPlaylist_)
    {
        scanExt = videoExt + ";" + imageExt;
    }
    else if (!currentPath.empty() && IsVideoFile(std::filesystem::path(currentPath), imageExt))
    {
        scanExt = imageExt;
    }
    else
    {
        scanExt = videoExt;
    }

    std::vector<std::string> files;
    ScanVideos(std::filesystem::path(watchedDir_), 0, maxDepth, scanExt, files);
    std::ranges::sort(files);

    entries_.clear();
    current_ = -1;
    cursor_ = -1;
    entries_.reserve(files.size());
    for (auto& f : files)
    {
        AddFile(std::move(f));
    }

    // Ensure the currently-playing file stays in the list even if not scanned.
    const bool currentFound = !currentPath.empty() && std::ranges::any_of(
                                  entries_,
                                  [&](const Entry& e)
                                  {
                                      return e.path == currentPath;
                                  }
                              );
    if (!currentPath.empty() && !currentFound)
    {
        AddFile(currentPath);
    }

    // Restore current_ by path match.
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i)
    {
        if (entries_[i].path == currentPath)
        {
            current_ = i;
            break;
        }
    }
    cursor_ = current_;

    if (shuffleEnabled_)
    {
        sortedEntries_ = entries_; // refresh backup with the newly scanned sorted list
        ApplyShuffleToEntries();
    }
}

void Playlist::ToggleShuffle()
{
    shuffleEnabled_ = !shuffleEnabled_;
    if (shuffleEnabled_)
    {
        // Save the sorted order and shuffle entries_ in-place.
        sortedEntries_ = entries_;
        ApplyShuffleToEntries();
    }
    else
    {
        // Restore sorted order, keeping the current file active.
        const std::string currentPath =
            current_ >= 0 && current_ < static_cast<int>(entries_.size()) ? entries_[current_].path : std::string{};
        entries_ = std::move(sortedEntries_);
        sortedEntries_.clear();
        current_ = -1;
        for (int i = 0; i < static_cast<int>(entries_.size()); ++i)
        {
            if (entries_[i].path == currentPath)
            {
                current_ = i;
                break;
            }
        }
        cursor_ = current_;
    }
}

void Playlist::ApplyShuffleToEntries()
{
    if (entries_.empty())
    {
        return;
    }

    const std::string currentPath =
        current_ >= 0 && current_ < static_cast<int>(entries_.size()) ? entries_[current_].path : std::string{};

    std::mt19937 rng(std::random_device{}());
    std::ranges::shuffle(entries_, rng);

    // Move current_ to front so Next() doesn't replay it immediately.
    if (!currentPath.empty())
    {
        const auto it = std::ranges::find_if(
            entries_,
            [&](const Entry& e)
            {
                return e.path == currentPath;
            }
        );
        if (it != entries_.end())
        {
            std::iter_swap(entries_.begin(), it);
        }
    }
    current_ = 0;
    cursor_ = 0;
}

// ── Keyboard navigation ───────────────────────────────────────────────────────

void Playlist::OnOpened()
{
    cursor_ = current_ >= 0 ? current_ : 0;
}

void Playlist::CursorUp()
{
    if (entries_.empty())
    {
        return;
    }

    if (cursor_ - 1 >= 0)
    {
        cursor_ -= 1;
    }
}

void Playlist::CursorDown()
{
    if (entries_.empty())
    {
        return;
    }

    const int n = static_cast<int>(entries_.size());
    if (cursor_ + 1 < n)
    {
        cursor_ += 1;
    }
}

void Playlist::ConfirmCursor()
{
    if (cursor_ >= 0 && cursor_ < static_cast<int>(entries_.size()))
    {
        Activate(cursor_);
    }
}

// ── Panel content ─────────────────────────────────────────────────────────────

void Playlist::RenderContent(const float panelW, float /*panelH*/, UIContext& ctx)
{
    constexpr float rowH = 44.f;
    constexpr float padding = 12.f;
    constexpr float headerH = 36.f;
    constexpr float popReserve = 26.f; // right-edge space reserved for the Panel pop-out toggle

    // ── Header ───────────────────────────────────────────────────────────────
    {
        const UI::Vec2 hdrMin = ctx.GetCursorScreenPos();
        const UI::Vec2 hdrMax = {hdrMin.x + panelW, hdrMin.y + headerH};
        auto& dl = ctx.GetWindowDrawList();

        dl.AddRectFilled(hdrMin, hdrMax, UI::MakeColor32(18, 10, 28, 230));

        // ─ Title – top-left (suppressed when popped: the OS title bar shows it) ─
        float counterX = padding;
        if (!IsPoppedOut())
        {
            ctx.SetCursorPosY(10.f);
            ctx.SetCursorPosX(padding);
            ctx.TextColored(UI::Color4f(0.88f, 0.82f, 1.f, 1.f), "Playlist");
            counterX = padding + 64.f;
        }

        // ─ Entry counter – beside the title ────────────────────────────────────
        if (!entries_.empty())
        {
            ctx.SetCursorPosY(10.f);
            ctx.SetCursorPosX(counterX);
            const std::string counter =
                (current_ >= 0 ? std::to_string(current_ + 1) : "-") + " / " + std::to_string(entries_.size());
            ctx.TextColored(UI::Color4f(0.5f, 0.45f, 0.65f, 1.f), counter.c_str());
        }

        // ─ Shuffle button (S) ──────────────────────────────────────────────────
        ctx.SetCursorPosY(8.f);
        ctx.SetCursorPosX(panelW - popReserve - padding - 22.f - 4.f - 22.f);
        if (shuffleEnabled_)
        {
            ctx.PushStyleColor(UI::ColorSlot::Button, UI::Color4f(0.45f, 0.20f, 0.75f, 0.90f));
            ctx.PushStyleColor(UI::ColorSlot::ButtonHovered, UI::Color4f(0.55f, 0.30f, 0.85f, 1.0f));
        }
        else
        {
            ctx.PushStyleColor(UI::ColorSlot::Button, UI::Color4f(0.15f, 0.10f, 0.25f, 0.70f));
            ctx.PushStyleColor(UI::ColorSlot::ButtonHovered, UI::Color4f(0.25f, 0.20f, 0.40f, 0.85f));
        }
        if (ctx.Button("S", {22.f, 20.f}))
        {
            ToggleShuffle();
        }
        ctx.PopStyleColor(2);

        // ─ Reload button (R) ──────────────────────────────────────────────────
        ctx.SetCursorPosY(8.f);
        ctx.SetCursorPosX(panelW - popReserve - padding - 22.f);
        ctx.PushStyleColor(UI::ColorSlot::Button, UI::Color4f(0.15f, 0.10f, 0.25f, 0.70f));
        ctx.PushStyleColor(UI::ColorSlot::ButtonHovered, UI::Color4f(0.25f, 0.20f, 0.40f, 0.85f));
        if (ctx.Button("R", {22.f, 20.f}))
        {
            Reload();
        }
        ctx.PopStyleColor(2);

        dl.AddLine(
            {hdrMin.x + padding, hdrMax.y - 1.f}, {hdrMin.x + panelW - padding, hdrMax.y - 1.f},
            UI::MakeColor32(80, 55, 120, 200)
        );

        ctx.SetCursorPosY(headerH);
    }

    ctx.BeginChild("##plitems", UI::Vec2(panelW, 0.f));

    for (int i = 0; i < static_cast<int>(entries_.size()); ++i)
    {
        const bool isPlaying = i == current_;
        const bool isCursor = i == cursor_;

        // Content-local Y of this row's top. Scroll-independent: ImGui applies the
        // scroll offset internally. Using window-local coordinates (not screen-derived
        // ones) keeps the content height stable while scrolling so ScrollY isn't clamped.
        const float rowTop = static_cast<float>(i) * rowH;
        ctx.SetCursorPosY(rowTop);

        const UI::Vec2 rowMin = ctx.GetCursorScreenPos();
        const UI::Vec2 rowMax = {rowMin.x + panelW, rowMin.y + rowH};
        auto& dl = ctx.GetWindowDrawList();

        if (isCursor && !isPlaying)
        {
            dl.AddRectFilled(rowMin, rowMax, UI::MakeColor32(60, 45, 90, 160));
        }
        if (isPlaying)
        {
            dl.AddRectFilled(rowMin, rowMax, UI::MakeColor32(90, 60, 160, 190));
        }

        ctx.PushID(i);
        ctx.SetCursorPosX(0.f);
        if (ctx.Selectable("##row", isCursor, UI::SelectableFlags::None, UI::Vec2(panelW, rowH - 2.f)))
        {
            cursor_ = i;
            Activate(i);
        }
        ctx.PopID();

        ctx.SetCursorPosX(padding);
        ctx.SetCursorPosY(rowTop + 6.f);

        const UI::Color4f nameCol = isPlaying
                                        ? UI::Color4f(1.f, 0.4f, 0.4f, 1.f)
                                        : isCursor
                                        ? UI::Color4f(1.f, 1.f, 1.f, 1.f)
                                        : UI::Color4f(0.82f, 0.78f, 0.9f, 1.f);
        ctx.TextColored(nameCol, entries_[i].label.c_str());

        ctx.SetCursorPosX(padding);
        ctx.SetCursorPosY(rowTop + 24.f);
        ctx.TextColored(UI::Color4f(0.45f, 0.42f, 0.55f, 1.f), entries_[i].path.c_str());

        dl.AddLine(
            {rowMin.x + padding, rowMax.y - 1.f}, {rowMax.x - padding, rowMax.y - 1.f}, UI::MakeColor32(70, 55, 100, 80)
        );
    }

    if (entries_.empty())
    {
        ctx.SetCursorPosY(40.f);
        ctx.SetCursorPosX(padding);
        ctx.TextColored(UI::Color4f(0.4f, 0.35f, 0.55f, 1.f), "No items. Open a file to begin.");
    }

    ctx.EndChild();
}

// ── Plugin settings page ──────────────────────────────────────────────────────

void Playlist::RenderSettingsContent(UIContext& ctx)
{
    Widgets::SectionHeader(ctx, "Scanning");
    Widgets::Checkbox(
        ctx, "Automatically reload playlist when directory changes",
        "Re-scans the directory for new or removed files whenever the OS reports a change.", autoReload_
    );
    Widgets::Checkbox(ctx, "Populate playlist from subdirectories", nullptr, scanSubdirs_);
    Widgets::SliderInt(
        ctx, "Subdirectory scan depth", "How many directory levels deep the scanner descends.", scanMaxDepth_, 0, 10
    );
    Widgets::Checkbox(
        ctx, "Allow mixed video+image playlist",
        "When enabled, opening any file scans the directory for both video and image files.", mixedPlaylist_
    );
    Widgets::SectionHeader(ctx, "Slideshow");
    Widgets::Checkbox(
        ctx, "Image slideshow (auto-advance)", "Automatically advance to the next image after the set duration.",
        imageSlideshow_
    );
    Widgets::SliderFloat(
        ctx, "Slideshow duration", "Seconds to display each image before advancing.", slideshowDuration_, 1.0f, 60.0f
    );
}

FRAMELIFT_PLUGIN_EXPORT(
    Playlist, {
    .name = "Playlist",
    .version = {FRAMELIFT_VERSION_MAJOR, FRAMELIFT_VERSION_MINOR, FRAMELIFT_VERSION_PATCH},
    .renderOrder = 10,
    .publisher = "FrameLift",
    .description = "Folder playlist and file navigation",
    }
)