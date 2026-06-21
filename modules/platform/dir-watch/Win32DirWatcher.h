#pragma once
#include <framelift/platform/IDirWatcher.h>
#include <memory>
// ReSharper disable once CppUnusedIncludeDirective
#include <string>
#include <thread>

// IDirWatcher backed by ReadDirectoryChangesW with overlapped I/O.
class Win32DirWatcher final : public IDirWatcher
{
public:
    Win32DirWatcher();
    ~Win32DirWatcher() override;

    void Watch(const char* rootDir, void (*onChange)(void* ud), void* ud, int maxDepth = 0) noexcept override;
    void Unwatch() noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    void (*onChange_)(void*) = nullptr;
    void* onChangeUd_ = nullptr;
    int maxDepth_ = 0;
    std::thread thread_;

    void ThreadProc() const;
};