// Windows-only translation unit. Compiled to nothing elsewhere so the explicit
// host source list can include it on every platform (see LinuxDirWatcher.cpp
// for the mirror guard).
#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <memory>
#include <string>
#include <windows.h>

#include "Win32DirWatcher.h"

// ── Impl (holds raw Win32 handles) ───────────────────────────────────────────
struct Win32DirWatcher::Impl
{
    HANDLE hDir = INVALID_HANDLE_VALUE;
    HANDLE hStop = INVALID_HANDLE_VALUE; // manual-reset event; set to stop thread
};

// Count backslash/forwardslash separators in a wide string of known character
// length (NOT byte length). One separator means one directory level deep.
static int PathDepth(const wchar_t* filename, const DWORD charCount)
{
    int depth = 0;
    for (DWORD i = 0; i < charCount; ++i)
    {
        if (filename[i] == L'\\' || filename[i] == L'/')
        {
            ++depth;
        }
    }
    return depth;
}

// ── Constructor / Destructor ──────────────────────────────────────────────────

Win32DirWatcher::Win32DirWatcher() : impl_(std::make_unique<Impl>())
{
}

Win32DirWatcher::~Win32DirWatcher()
{
    Unwatch();
}

// ── Watch ─────────────────────────────────────────────────────────────────────

void Win32DirWatcher::Watch(const char* rootDir, void (*onChange)(void*), void* ud, const int maxDepth) noexcept
{
    Unwatch();
    onChange_ = onChange;
    onChangeUd_ = ud;
    maxDepth_ = maxDepth;

    // Convert UTF-8 path to wide string for the Win32 API.
    const int wLen = MultiByteToWideChar(CP_UTF8, 0, rootDir, -1, nullptr, 0);
    std::wstring wDir(static_cast<size_t>(wLen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, rootDir, -1, wDir.data(), wLen);

    impl_->hDir = CreateFileW(
        wDir.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr
    );

    if (impl_->hDir == INVALID_HANDLE_VALUE)
    {
        return;
    }

    impl_->hStop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (impl_->hStop == nullptr)
    {
        CloseHandle(impl_->hDir);
        impl_->hDir = INVALID_HANDLE_VALUE;
        return;
    }

    thread_ = std::thread(&Win32DirWatcher::ThreadProc, this);
}

// ── Unwatch ───────────────────────────────────────────────────────────────────

void Win32DirWatcher::Unwatch() noexcept
{
    // Signal the thread to stop, then join it before closing handles.
    if (impl_->hStop != INVALID_HANDLE_VALUE)
    {
        SetEvent(impl_->hStop);
    }

    if (thread_.joinable())
    {
        thread_.join();
    }

    if (impl_->hDir != INVALID_HANDLE_VALUE)
    {
        CloseHandle(impl_->hDir);
        impl_->hDir = INVALID_HANDLE_VALUE;
    }
    if (impl_->hStop != INVALID_HANDLE_VALUE)
    {
        CloseHandle(impl_->hStop);
        impl_->hStop = INVALID_HANDLE_VALUE;
    }
    onChange_ = nullptr;
    onChangeUd_ = nullptr;
}

// ── Background thread ─────────────────────────────────────────────────────────

void Win32DirWatcher::ThreadProc() const
{
    // maxDepth_ == 0 → root only (non-recursive), no depth filtering needed.
    // maxDepth_ >  0 → recursive, fire only for depth ≤ maxDepth_.
    // maxDepth_ <  0 → recursive, no depth filtering (unlimited).
    const BOOL watchSubtree = maxDepth_ != 0 ? TRUE : FALSE;

    alignas(DWORD) char buf[32768];
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (ov.hEvent == nullptr)
    {
        return;
    }

    while (true)
    {
        DWORD bytes = 0;
        ReadDirectoryChangesW(
            impl_->hDir, buf, sizeof(buf), watchSubtree, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME,
            &bytes, &ov, nullptr
        );

        const HANDLE handles[2] = {ov.hEvent, impl_->hStop};
        const DWORD wait = WaitForMultipleObjects(2, handles, FALSE, INFINITE);

        if (wait == WAIT_OBJECT_0 + 1)
        {
            // Stop event — cancel pending I/O and drain the completion.
            CancelIo(impl_->hDir);
            WaitForSingleObject(ov.hEvent, INFINITE);
            break;
        }

        if (wait != WAIT_OBJECT_0)
        {
            break; // unexpected error
        }

        if (!GetOverlappedResult(impl_->hDir, &ov, &bytes, FALSE))
        {
            break;
        }

        ResetEvent(ov.hEvent);

        if (bytes == 0)
        {
            // Buffer overflow: we lost change details but know something changed.
            if (onChange_)
            {
                onChange_(onChangeUd_);
            }
            continue;
        }

        // Walk the FILE_NOTIFY_INFORMATION chain. Fire once per batch if any
        // entry passes the depth filter — avoids redundant Reload() calls.
        const auto* fni = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(buf);
        bool shouldNotify = false;

        while (true)
        {
            if (fni->Action == FILE_ACTION_ADDED || fni->Action == FILE_ACTION_REMOVED ||
                fni->Action == FILE_ACTION_RENAMED_OLD_NAME || fni->Action == FILE_ACTION_RENAMED_NEW_NAME)
            {
                const DWORD charCount = fni->FileNameLength / sizeof(wchar_t);
                const int depth = PathDepth(fni->FileName, charCount);

                // maxDepth_ < 0  → unlimited (always passes)
                // maxDepth_ >= 0 → must be within the configured depth
                if (maxDepth_ < 0 || depth <= maxDepth_)
                {
                    shouldNotify = true;
                    break;
                }
            }

            if (fni->NextEntryOffset == 0)
            {
                break;
            }

            fni = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(
                reinterpret_cast<const char*>(fni) + fni->NextEntryOffset
            );
        }

        if (shouldNotify && onChange_)
        {
            onChange_(onChangeUd_);
        }
    }

    CloseHandle(ov.hEvent);
}

#endif // _WIN32
