#pragma once

#include <framelift/services/IFrameSampler.h>

#include <cstring>

// Minimal in-memory IFrameSampler for tests: every Open() succeeds (unless failOpen),
// every ReadFrameRGBA() returns a zeroed frame of the configured size. Counts calls so
// tests can assert how many frames the worker actually sampled.
class FakeFrameSampler final : public IFrameSampler
{
public:
    double duration = 20.0;
    int width = 8;
    int height = 8;
    bool failOpen = false;
    int openCalls = 0;
    int readCalls = 0;

    [[nodiscard]] void* Open(const char*) noexcept override
    {
        if (failOpen)
        {
            return nullptr;
        }
        ++openCalls;
        return new int(1); // opaque non-null handle
    }

    void Close(void* s) noexcept override
    {
        delete static_cast<int*>(s);
    }

    [[nodiscard]] double DurationSec(const void*) const noexcept override
    {
        return duration;
    }

    [[nodiscard]] bool NativeSize(const void*, int* w, int* h) const noexcept override
    {
        if (w)
        {
            *w = width;
        }
        if (h)
        {
            *h = height;
        }
        return true;
    }

    [[nodiscard]] bool ReadFrameRGBA(
        void*, double posSec, int outW, int outH, unsigned char* buf, int cap, double* actualSec
    ) noexcept override
    {
        ++readCalls;
        const int w = (outW == 0 && outH == 0) ? width : outW;
        const int h = (outW == 0 && outH == 0) ? height : outH;
        const int need = w * h * 4;
        if (!buf || cap < need)
        {
            return false;
        }
        std::memset(buf, 0, static_cast<std::size_t>(need));
        if (actualSec)
        {
            *actualSec = posSec;
        }
        return true;
    }
};
