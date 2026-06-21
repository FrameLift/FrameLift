#pragma once
#include <framelift/platform/IDirWatcher.h>
#include <memory>

// Constructs the platform's IDirWatcher implementation: Win32DirWatcher
// (ReadDirectoryChangesW) on Windows, LinuxDirWatcher (inotify) on Linux. Keeps
// App.cpp free of any concrete platform watcher type.
std::unique_ptr<IDirWatcher> CreateDirWatcher();