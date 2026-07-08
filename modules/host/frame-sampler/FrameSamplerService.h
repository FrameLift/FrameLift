#pragma once

#include <framelift/services/IFrameSampler.h>

// Host implementation of IFrameSampler. Stateless itself — each Open() heap-allocates
// an independent demux + software-decode session (SamplerSession, defined in the .cpp
// where libav headers live), so App.cpp can construct/register this without pulling in
// <libav*>. See IFrameSampler.h for the threading and lifetime contract.
class FrameSamplerService final : public IFrameSampler
{
public:
    FrameSamplerService() = default;
    ~FrameSamplerService() override = default;

    [[nodiscard]] void* Open(const char* path) noexcept override;
    void Close(void* session) noexcept override;
    [[nodiscard]] double DurationSec(const void* session) const noexcept override;
    [[nodiscard]] bool NativeSize(const void* session, int* w, int* h) const noexcept override;
    [[nodiscard]] bool ReadFrameRGBA(
        void* session, double posSec, int outW, int outH, unsigned char* buf, int cap, double* actualSec
    ) noexcept override;
};
