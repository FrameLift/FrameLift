#include "Playlist.h"
#include "ExtensionFilter.h"
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "Version.h"
#include <framelift/core.h>
#include <framelift/platform.h>

#include <QtCore/QVariantMap>
#include <algorithm>
#include <filesystem>
#include <numeric>
#include <random>
#include <thread>

// Read a string setting via the ABI-stable per-key getter (host owns the value).
// Falls back to the supplied default if no context is available.
static std::string ReadStringSetting(IModuleContext* ctx, const char* key, const char* fallback)
{
    auto* store = ctx ? ctx->GetService<ISettingsStore>() : nullptr;
    if (!store)
    {
        return fallback;
    }
    const int n = store->GetSettingString(key, nullptr, 0);
    std::string s(static_cast<std::size_t>(n), '\0');
    if (n > 0)
    {
        store->GetSettingString(key, s.data(), n + 1);
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
    // Tell any in-flight scan worker not to touch the event pump after we're gone.
    if (scanShared_)
    {
        scanShared_->alive = false;
    }
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

// ── ModuleBase hooks ───────────────────────────────────────────────────────

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
             togglePanel();
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

void Playlist::OnInstall(IModuleContext& ctx)
{
    if (auto* events = ctx.GetService<IEventPump>())
    {
        dirChangedEventType_ = events->RegisterCustomEventType();
        scanDoneEventType_ = events->RegisterCustomEventType();
        scanShared_->events = events;
        scanShared_->doneEventType = scanDoneEventType_;
    }

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

    if (auto* menu = ctx.GetService<ContextMenu>())
    {
        framelift::AddSection(
            *menu,
            [this](ContextMenu& m)
            {
                framelift::AddItem(
                    m, "Playlist", "togglePlaylist",
                    [this]
                    {
                        togglePanel();
                    }
                );
            }
        );
    }
}

// ── IModule ──────────────────────────────────────────────────────────────────

bool Playlist::HandleEvent(const AppEvent& e)
{
    if (e.type == AppEventType::Quit)
    {
        FlushCurrentPos();
        return false; // don't consume — let other plugins see Quit too
    }
    if (e.type == AppEventType::Custom)
    {
        const uint32_t et = e.AsCustom().eventType;
        if (dirChangedEventType_ != 0 && et == dirChangedEventType_)
        {
            Reload();
            return false;
        }
        if (scanDoneEventType_ != 0 && et == scanDoneEventType_)
        {
            ApplyScanResult();
            return false;
        }
    }
    return ModuleBase::HandleEvent(e);
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

    auto* player = ctx_ ? ctx_->GetService<IMediaPlayback>() : nullptr;
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
    const std::string pathStr(path);

    // Start playback immediately with a provisional single-item playlist. The full
    // directory listing is scanned on a background thread and swapped in by
    // ApplyScanResult() — so a large/nested folder never blocks the start of
    // playback or the UI thread (see ScanShared in the header).
    Clear();
    AddFile(pathStr);
    current_ = 0;
    cursor_ = 0;
    LoadFile(pathStr.c_str());

    StartScan(pathStr);
}

void Playlist::StartScan(const std::string& path)
{
    if (!scanShared_)
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

    // Claim a generation so a later OpenFile() supersedes this scan's result.
    const uint64_t gen = scanShared_->latestGen.fetch_add(1) + 1;

    // Runs the directory walk and publishes into the shared slot; returns false if
    // a newer open superseded us in the meantime. Heavy work happens here.
    auto scan = [shared = scanShared_, dir, scanExt, maxDepth, gen, path]
    {
        std::vector<std::string> files;
        ScanVideos(dir, 0, maxDepth, scanExt, files);

        std::lock_guard<std::mutex> lk(shared->mtx);
        if (gen != shared->latestGen.load())
        {
            return false; // superseded by a newer open
        }
        shared->files = std::move(files);
        shared->dir = dir.string();
        shared->openedPath = path;
        shared->gen = gen;
        shared->ready = true;
        return true;
    };

    // Without an event pump we can't marshal the result back to the UI thread, so
    // scan synchronously and apply inline (also keeps headless/unit-test runs
    // deterministic). With a pump, offload to a detached worker so a large folder
    // never blocks the UI thread or the start of playback.
    if (!scanShared_->events)
    {
        if (scan())
        {
            ApplyScanResult();
        }
        return;
    }

    std::thread(
        [shared = scanShared_, scan = std::move(scan)]
        {
            if (scan() && shared->alive.load() && shared->events)
            {
                shared->events->PushCustomEvent(shared->doneEventType);
            }
        }
    ).detach();
}

void Playlist::ApplyScanResult()
{
    if (!scanShared_)
    {
        return;
    }

    std::vector<std::string> files;
    std::string dir;
    std::string openedPath;
    {
        std::lock_guard<std::mutex> lk(scanShared_->mtx);
        if (!scanShared_->ready || scanShared_->gen != scanShared_->latestGen.load())
        {
            return; // nothing ready, or superseded by a newer open
        }
        files = std::move(scanShared_->files);
        dir = std::move(scanShared_->dir);
        openedPath = std::move(scanShared_->openedPath);
        scanShared_->ready = false;
    }

    // Keep whatever is currently playing selected, without restarting playback.
    const std::string keepPath =
        current_ >= 0 && current_ < static_cast<int>(entries_.size()) ? entries_[current_].path : openedPath;
    RebuildEntries(files, keepPath);

    // (Re)arm the directory watcher for the now-listed directory.
    watchedDir_ = dir;
    auto* dw = ctx_ ? ctx_->GetService<IDirWatcher>() : nullptr;
    auto* events = ctx_ ? ctx_->GetService<IEventPump>() : nullptr;
    if (dw && events)
    {
        struct Ctx
        {
            Playlist* self;
            IEventPump* events;
        };

        static const auto cb = [](void* ud)
        {
            auto* c = static_cast<Ctx*>(ud);
            if (c->self->autoReload_)
            {
                c->events->PushCustomEvent(c->self->dirChangedEventType_);
            }
        };
        watchCbCtx_ = {this, events};
        dw->Watch(watchedDir_.c_str(), cb, &watchCbCtx_, scanSubdirs_ ? scanMaxDepth_ : 0);
    }
}

// ── Entry management ──────────────────────────────────────────────────────────

void Playlist::AddFile(std::string path)
{
    auto label = FilenameOf(path);
    entries_.emplace_back(std::move(path), std::move(label));
    Q_EMIT playlistChanged();
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
    Q_EMIT playlistChanged();
}

void Playlist::Activate(const int index)
{
    if (index < 0 || index >= static_cast<int>(entries_.size()))
    {
        return;
    }
    current_ = index;
    LoadFile(entries_[current_].path.c_str());
    Q_EMIT playlistChanged();
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
    RebuildEntries(files, currentPath);
}

void Playlist::RebuildEntries(std::vector<std::string>& files, const std::string& keepPath)
{
    std::ranges::sort(files);

    entries_.clear();
    current_ = -1;
    cursor_ = -1;
    entries_.reserve(files.size() + 1);
    for (auto& f : files)
    {
        AddFile(std::move(f));
    }

    // Ensure the file to keep stays in the list even if it wasn't scanned.
    const bool keepFound = !keepPath.empty() && std::ranges::any_of(
                                                    entries_,
                                                    [&](const Entry& e)
                                                    {
                                                        return e.path == keepPath;
                                                    }
                                                );
    if (!keepPath.empty() && !keepFound)
    {
        AddFile(keepPath);
    }

    // Restore current_ by path match.
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i)
    {
        if (entries_[i].path == keepPath)
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
    Q_EMIT playlistChanged();
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
    Q_EMIT playlistChanged();
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

void Playlist::CursorUp()
{
    if (entries_.empty())
    {
        return;
    }

    if (cursor_ - 1 >= 0)
    {
        cursor_ -= 1;
        Q_EMIT playlistChanged();
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
        Q_EMIT playlistChanged();
    }
}

void Playlist::ConfirmCursor()
{
    if (cursor_ >= 0 && cursor_ < static_cast<int>(entries_.size()))
    {
        Activate(cursor_);
    }
}

QVariantList Playlist::QmlEntries() const
{
    QVariantList result;
    result.reserve(static_cast<qsizetype>(entries_.size()));
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i)
    {
        QVariantMap row;
        row.insert(QStringLiteral("label"), QString::fromStdString(entries_[i].label));
        row.insert(QStringLiteral("path"), QString::fromStdString(entries_[i].path));
        row.insert(QStringLiteral("current"), i == current_);
        row.insert(QStringLiteral("cursor"), i == cursor_);
        result.push_back(row);
    }
    return result;
}

void Playlist::togglePanel()
{
    SetOpen(!open_);
    Q_EMIT panelStateChanged();
}

void Playlist::activateIndex(const int index)
{
    Activate(index);
}

void Playlist::publishVisibleWidth(const qreal width)
{
    if (ctx_)
    {
        ctx_->Publish<PanelLayoutEvent>({0, static_cast<float>(width)});
    }
}

void Playlist::SetOpen(const bool value)
{
    if (open_ == value)
    {
        return;
    }
    open_ = value;
    if (open_)
    {
        cursor_ = current_ >= 0 ? current_ : 0;
        if (auto* focus = ctx_ ? ctx_->GetService<FocusManager>() : nullptr)
        {
            focus->Acquire(this);
        }
    }
    else
    {
        if (auto* focus = ctx_ ? ctx_->GetService<FocusManager>() : nullptr)
        {
            focus->Release(this);
        }
        if (ctx_)
        {
            ctx_->Publish<PanelLayoutEvent>({0, 0.f});
        }
    }
    Q_EMIT playlistChanged();
}
