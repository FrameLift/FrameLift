#pragma once
#include <framelift/platform/IDirWatcher.h>
#include <memory>
#include <string>
#include <thread>

// IDirWatcher backed by Linux inotify. inotify is not natively recursive, so a
// watch is added per directory up to the configured depth, and new subdirectories
// are picked up at runtime to mirror Win32's ReadDirectoryChangesW subtree watch.
class LinuxDirWatcher final : public IDirWatcher
{
public:
    LinuxDirWatcher();
    ~LinuxDirWatcher() override;

    void Watch(const char* rootDir, void (*onChange)(void* ud), void* ud, int maxDepth = 0) noexcept override;
    void Unwatch() noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    void (*onChange_)(void*) = nullptr;
    void* onChangeUd_ = nullptr;
    int maxDepth_ = 0;
    std::thread thread_;

    // depth == 0 is always the root. depth > 0 levels qualify only when maxDepth_
    // is unlimited (< 0) or within bound (> 0 && depth <= maxDepth_).
    bool Qualifies(int depth) const;
    void AddWatchRecursive(const std::string& path, int depth);
    void ThreadProc();
};
