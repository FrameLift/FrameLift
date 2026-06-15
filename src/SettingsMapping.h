#pragma once

#include "Settings.h"
#include <framelift/platform/IMediaPlayer.h>

// Pure mappers from the host Settings struct to the POD player option structs.
// Extracted from App.cpp so they can be unit-tested without pulling in SDL/FFmpeg.

inline AudioNormalizeParams ParamsFromSettings(const Settings& s)
{
    return {s.dynaudnormFrameLen, s.dynaudnormGaussSize, s.dynaudnormPeak, s.dynaudnormMaxGain, s.dynaudnormVolume};
}

inline PlaybackOptions PlaybackOptsFromSettings(const Settings& s)
{
    return {s.hwdec, s.hrSeek, s.subAutoLoad, s.audioFileAutoLoad};
}

inline ReadAheadCacheOptions ReadAheadOptsFromSettings(const Settings& s)
{
    const int64_t mb = s.readAheadSizeMB > 0 ? s.readAheadSizeMB : 0;
    return {s.readAheadEnabled, mb * 1024 * 1024};
}