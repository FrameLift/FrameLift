#pragma once

#include <framelift/platform/IMediaPlayer.h>

inline int AudioOutputChannelsForMode(const AudioChannelMode mode) noexcept
{
    switch (mode)
    {
    case AudioChannelMode::Mono:
        return 1;
    case AudioChannelMode::Stereo:
        return 2;
    case AudioChannelMode::Surround:
        return 6;
    case AudioChannelMode::Auto:
    default:
        return 2;
    }
}

