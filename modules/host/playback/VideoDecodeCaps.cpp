#include "VideoDecodeCaps.h"

#include "FFmpegHwDecode.h"
#include "VideoDecodeMode.h"

#include <framelift/Log.h>

void VideoDecodeCaps::EnsureProbed() const
{
    if (probed_)
    {
        return;
    }
    probed_ = true;

    // Probe each distinct hardware backend at most once — several modes (e.g. vulkan
    // and vulkan-zero-copy) share one device, so cache the per-backend result. Indexed
    // by HwBackend enum value (None..VAAPI ⇒ 0..5).
    bool probedResult[8] = {};
    bool haveResult[8] = {};

    for (const VideoDecodeMode mode : CandidateVideoDecodeModes())
    {
        if (mode == VideoDecodeMode::Off || mode == VideoDecodeMode::Auto)
        {
            modes_.emplace_back(VideoDecodeModeName(mode)); // always available
            continue;
        }

        const HwBackend backend = HwBackendForProbe(mode);
        if (backend == HwBackend::None)
        {
            continue; // not a probeable hardware mode in this build
        }

        const auto idx = static_cast<std::size_t>(backend);
        if (!haveResult[idx])
        {
            probedResult[idx] = ProbeHwBackendAvailable(backend);
            haveResult[idx] = true;
            const char* avail = probedResult[idx] ? "available" : "unavailable";
            Log::Debug("VideoDecodeCaps: {} device {}", HwBackendName(backend), avail);
        }
        if (probedResult[idx])
        {
            modes_.emplace_back(VideoDecodeModeName(mode));
        }
    }
}

void VideoDecodeCaps::EnumerateAvailableModes(void (*visit)(const char* token, void* ud), void* ud) const noexcept
{
    if (!visit)
    {
        return;
    }
    EnsureProbed();
    for (const std::string& token : modes_)
    {
        visit(token.c_str(), ud);
    }
}
