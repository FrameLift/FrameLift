// Unit tests for the FFmpeg backend's pure hardware-decode selection surface
// (issue #25, Phase 7). Only PreferredHwBackends()/HwBackendName() are exercised —
// they are libav-free (inline in the header) so they build in the standalone native
// test suite. The libav-touching FFmpegHwDecode class needs real codecs + a GPU and
// is verified manually via the DebugOverlay/Benchmark plugins.

#include "platform/ffmpeg/FFmpegHwDecode.h"

#include <gtest/gtest.h>

#include <algorithm>

// ── PreferredHwBackends ─────────────────────────────────────────────────────────

TEST(FFmpegHwDecodeTests, PrefersCudaFirst)
{
    // NVIDIA nvdec leads on every platform (issue #25 decision).
    const auto order = PreferredHwBackends();
    ASSERT_FALSE(order.empty());
    EXPECT_EQ(order.front(), HwBackend::Cuda);
}

TEST(FFmpegHwDecodeTests, PlatformNativeBackendsFollowCuda)
{
    const auto order = PreferredHwBackends();
#if defined(_WIN32)
    EXPECT_EQ(order, (std::vector<HwBackend>{HwBackend::Cuda, HwBackend::D3D11VA, HwBackend::DXVA2}));
#else
    EXPECT_EQ(order, (std::vector<HwBackend>{HwBackend::Cuda, HwBackend::VAAPI}));
#endif
}

TEST(FFmpegHwDecodeTests, PreferenceListHasNoNoneAndNoDuplicates)
{
    const auto order = PreferredHwBackends();
    for (const HwBackend b : order)
    {
        EXPECT_NE(b, HwBackend::None);
    }
    auto sorted = order;
    std::sort(sorted.begin(), sorted.end());
    EXPECT_EQ(std::adjacent_find(sorted.begin(), sorted.end()), sorted.end());
}

// ── HwBackendName ───────────────────────────────────────────────────────────────

TEST(FFmpegHwDecodeTests, BackendNamesMatchHwaccelStrings)
{
    // Benchmark.cpp treats empty/"no"/"N/A" as software and shows "Hardware (<name>)"
    // otherwise — these names must be non-empty for every real backend.
    EXPECT_STREQ(HwBackendName(HwBackend::Vulkan), "vulkan");
    EXPECT_STREQ(HwBackendName(HwBackend::Cuda), "cuda");
    EXPECT_STREQ(HwBackendName(HwBackend::D3D11VA), "d3d11va");
    EXPECT_STREQ(HwBackendName(HwBackend::DXVA2), "dxva2");
    EXPECT_STREQ(HwBackendName(HwBackend::VAAPI), "vaapi");
}

TEST(FFmpegHwDecodeTests, NoneHasEmptyName)
{
    EXPECT_STREQ(HwBackendName(HwBackend::None), "");
}

TEST(FFmpegHwDecodeTests, EveryPreferredBackendHasANonEmptyName)
{
    for (const HwBackend b : PreferredHwBackends())
    {
        EXPECT_STRNE(HwBackendName(b), "");
    }
}
