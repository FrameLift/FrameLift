#pragma once

#include <cstdio>
#include <string>

// Minimal semantic-version value used by the Updater to compare the running
// build against the latest release tag. Platform-independent (no Win32) so it
// can be unit-tested natively.

struct SemVer
{
    int major = 0, minor = 0, patch = 0;

    [[nodiscard]] bool operator>(const SemVer& o) const
    {
        if (major != o.major)
        {
            return major > o.major;
        }
        if (minor != o.minor)
        {
            return minor > o.minor;
        }
        return patch > o.patch;
    }
};

// Parse "MAJOR.MINOR.PATCH" (extra components ignored). Missing components stay 0.
inline SemVer ParseVersion(const std::string& s)
{
    SemVer v;
    std::sscanf(s.c_str(), "%d.%d.%d", &v.major, &v.minor, &v.patch);
    return v;
}
