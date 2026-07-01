#include "AppPaths.h"
#include <cstddef>
#include <cstring>

int AppPaths::GetPrefPath(char* buf, int cap) const noexcept
{
    const int len = static_cast<int>(prefPath_.size());
    if (buf && cap > 0)
    {
        const int n = len < cap - 1 ? len : cap - 1;
        std::memcpy(buf, prefPath_.c_str(), static_cast<std::size_t>(n));
        buf[n] = '\0';
    }
    return len;
}
