#pragma once

#include <framelift/platform/IMediaPlayer.h>

#include <string>
#include <vector>

// Host provider for IVideoDecodeCaps: the set of acceleration modes this machine
// can actually use. Standalone (not the player) — the answer depends only on the
// GPU/driver, not on any loaded file — so App owns one and registers it alongside
// the player facets, mirroring GraphicsInfoService / IGraphicsInfo.
//
// The probe (av_hwdevice_ctx_create per candidate backend) costs tens of ms on
// success, so it runs once, lazily, on the first EnumerateAvailableModes call
// (i.e. when the user first opens the Playback settings page) and the resulting
// token list is cached for the process lifetime. Main/UI-thread use only.
class VideoDecodeCaps final : public IVideoDecodeCaps
{
public:
    VideoDecodeCaps() = default;

    void EnumerateAvailableModes(void (*visit)(const char* token, void* ud), void* ud) const noexcept override;

private:
    void EnsureProbed() const;

    mutable bool probed_ = false;
    mutable std::vector<std::string> modes_; // available tokens, in menu order
};
