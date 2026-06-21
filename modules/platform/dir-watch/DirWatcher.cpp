#include "DirWatcher.h"
#include <memory>

#ifdef _WIN32
#include "Win32DirWatcher.h"
#else
#include "LinuxDirWatcher.h"
#endif

std::unique_ptr<IDirWatcher> CreateDirWatcher()
{
#ifdef _WIN32
    return std::make_unique<Win32DirWatcher>();
#else
    return std::make_unique<LinuxDirWatcher>();
#endif
}
