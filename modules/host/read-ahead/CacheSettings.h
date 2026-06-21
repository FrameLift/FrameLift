#pragma once

#include "SettingsRegistry.h"

#include <cstdint>
#include <framelift/platform/IMediaPlayer.h>

// Read-ahead cache settings + mapping — owned by the host/read-ahead module.

struct CacheSettings
{
    bool readAheadEnabled = true;
    int readAheadSizeMB = 64;
};

inline void RegisterCacheSettings(SettingsRegistry& reg, CacheSettings& s)
{
    reg.AddBool("cache.readAheadEnabled", s.readAheadEnabled,
                "Enable the memory-bounded demuxer read-ahead cache.");
    reg.AddInt("cache.readAheadSizeMB", s.readAheadSizeMB,
               "Read-ahead demuxer cache size in MB (total across audio/video/subtitle).");
}

inline ReadAheadCacheOptions ToReadAheadCacheOptions(const CacheSettings& s)
{
    const int64_t mb = s.readAheadSizeMB > 0 ? s.readAheadSizeMB : 0;
    return {s.readAheadEnabled, mb * 1024 * 1024};
}
