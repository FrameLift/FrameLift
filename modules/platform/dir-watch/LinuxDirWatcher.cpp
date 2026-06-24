// Linux-only translation unit. Compiled to nothing elsewhere so the explicit
// host source list can include it on every platform (see Win32DirWatcher.cpp
// for the mirror guard).
#ifndef _WIN32

#include "LinuxDirWatcher.h"
#include <memory>
#include <string>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <dirent.h>
#include <poll.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

// ── Impl (holds the inotify fd, stop self-pipe, and per-watch bookkeeping) ─────
struct LinuxDirWatcher::Impl
{
    int fd = -1;                                 // inotify instance
    int stopPipe[2] = {-1, -1};                  // self-pipe: write to [1] to break poll() in Unwatch
    std::unordered_map<int, int> wdDepth;        // watch descriptor → depth from root
    std::unordered_map<int, std::string> wdPath; // watch descriptor → absolute dir path
};

// ── Constructor / Destructor ──────────────────────────────────────────────────

LinuxDirWatcher::LinuxDirWatcher() : impl_(std::make_unique<Impl>())
{
}

LinuxDirWatcher::~LinuxDirWatcher()
{
    Unwatch();
}

// ── Depth policy ──────────────────────────────────────────────────────────────

bool LinuxDirWatcher::Qualifies(const int depth) const
{
    if (depth == 0)
    {
        return true; // root always
    }
    if (maxDepth_ < 0)
    {
        return true; // unlimited recursion
    }
    if (maxDepth_ == 0)
    {
        return false; // root only
    }
    return depth <= maxDepth_;
}

// ── Watch-set construction ────────────────────────────────────────────────────

void LinuxDirWatcher::AddWatchRecursive(const std::string& path, const int depth)
{
    if (!Qualifies(depth))
    {
        return;
    }

    constexpr uint32_t mask = IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_ONLYDIR;
    const int wd = inotify_add_watch(impl_->fd, path.c_str(), mask);
    if (wd < 0)
    {
        return;
    }
    impl_->wdDepth[wd] = depth;
    impl_->wdPath[wd] = path;

    // Recurse into existing subdirectories only if the next level still qualifies.
    if (!Qualifies(depth + 1))
    {
        return;
    }

    DIR* dir = opendir(path.c_str());
    if (!dir)
    {
        return;
    }

    while (const dirent* e = readdir(dir))
    {
        if (std::strcmp(e->d_name, ".") == 0 || std::strcmp(e->d_name, "..") == 0)
        {
            continue;
        }

        std::string child = path;
        if (!child.empty() && child.back() != '/')
        {
            child += '/';
        }
        child += e->d_name;

        bool isDir = e->d_type == DT_DIR;
        if (e->d_type == DT_UNKNOWN)
        {
            struct stat st{};
            if (stat(child.c_str(), &st) == 0)
            {
                isDir = S_ISDIR(st.st_mode);
            }
        }
        if (isDir)
        {
            AddWatchRecursive(child, depth + 1);
        }
    }
    closedir(dir);
}

// ── Watch ─────────────────────────────────────────────────────────────────────

void LinuxDirWatcher::Watch(const char* rootDir, void (*onChange)(void*), void* ud, const int maxDepth) noexcept
{
    Unwatch();
    onChange_ = onChange;
    onChangeUd_ = ud;
    maxDepth_ = maxDepth;

    impl_->fd = inotify_init1(IN_CLOEXEC);
    if (impl_->fd < 0)
    {
        return;
    }

    if (pipe(impl_->stopPipe) != 0)
    {
        close(impl_->fd);
        impl_->fd = -1;
        return;
    }

    AddWatchRecursive(rootDir, 0);

    thread_ = std::thread(&LinuxDirWatcher::ThreadProc, this);
}

// ── Unwatch ───────────────────────────────────────────────────────────────────

void LinuxDirWatcher::Unwatch() noexcept
{
    // Wake the thread out of poll(), then join before closing the fds it uses.
    if (impl_->stopPipe[1] != -1)
    {
        const char b = 1;
        (void)write(impl_->stopPipe[1], &b, 1);
    }

    if (thread_.joinable())
    {
        thread_.join();
    }

    if (impl_->fd != -1)
    {
        close(impl_->fd);
        impl_->fd = -1;
    }
    if (impl_->stopPipe[0] != -1)
    {
        close(impl_->stopPipe[0]);
        impl_->stopPipe[0] = -1;
    }
    if (impl_->stopPipe[1] != -1)
    {
        close(impl_->stopPipe[1]);
        impl_->stopPipe[1] = -1;
    }
    impl_->wdDepth.clear();
    impl_->wdPath.clear();
    onChange_ = nullptr;
    onChangeUd_ = nullptr;
}

// ── Background thread ─────────────────────────────────────────────────────────

void LinuxDirWatcher::ThreadProc()
{
    // Large enough to drain a burst of events in one read; inotify guarantees
    // whole records, never a partial one.
    std::vector<char> buf(64 * 1024);

    while (true)
    {
        pollfd fds[2];
        fds[0] = {impl_->fd, POLLIN, 0};
        fds[1] = {impl_->stopPipe[0], POLLIN, 0};

        const int r = poll(fds, 2, -1);
        if (r < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            break;
        }

        if (fds[1].revents & POLLIN)
        {
            break; // stop requested
        }
        if (!(fds[0].revents & POLLIN))
        {
            continue;
        }

        const ssize_t n = read(impl_->fd, buf.data(), buf.size());
        if (n <= 0)
        {
            if (n < 0 && errno == EINTR)
            {
                continue;
            }
            break;
        }

        // Walk the event batch; fire once if anything relevant changed, and start
        // watching any newly-created subdirectory so deeper changes stay observed.
        bool shouldNotify = false;
        for (ssize_t off = 0; off < n;)
        {
            const auto* ev = reinterpret_cast<const inotify_event*>(buf.data() + off);

            if (ev->mask & (IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO))
            {
                shouldNotify = true;
            }

            if ((ev->mask & IN_ISDIR) && (ev->mask & (IN_CREATE | IN_MOVED_TO)) && ev->len > 0)
            {
                const auto itDepth = impl_->wdDepth.find(ev->wd);
                const auto itPath = impl_->wdPath.find(ev->wd);
                if (itDepth != impl_->wdDepth.end() && itPath != impl_->wdPath.end())
                {
                    std::string child = itPath->second;
                    if (!child.empty() && child.back() != '/')
                    {
                        child += '/';
                    }
                    child += ev->name;
                    AddWatchRecursive(child, itDepth->second + 1);
                }
            }

            off += static_cast<ssize_t>(sizeof(inotify_event)) + ev->len;
        }

        if (shouldNotify && onChange_)
        {
            onChange_(onChangeUd_);
        }
    }
}

#endif // !_WIN32
