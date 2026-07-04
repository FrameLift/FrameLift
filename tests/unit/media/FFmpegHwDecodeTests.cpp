// Unit tests for the FFmpeg backend's pure hardware-decode selection surface
// (issue #25, Phase 7). Only PreferredHwBackends()/HwBackendName() are exercised —
// they are libav-free (inline in the header) so they build in the standalone native
// test suite. The libav-touching FFmpegHwDecode class needs real codecs + a GPU and
// is verified manually via the DebugOverlay/Benchmark plugins.

#include "FFmpegHwDecode.h"
#include "VideoDecodeMode.h"

#include "QtTestRunner.h"

#include <QtTest/QtTest>

#include <algorithm>

// ── PreferredHwBackends ─────────────────────────────────────────────────────────

class FFmpegHwDecodeTests final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void PrefersCudaFirst()
    {
        // NVIDIA nvdec leads on every platform (issue #25 decision).
        const auto order = PreferredHwBackends();
        QVERIFY(!(order.empty()));
        QVERIFY((order.front()) == (HwBackend::Cuda));
    }

    void PlatformNativeBackendsFollowCuda()
    {
        const auto order = PreferredHwBackends();
#if defined(_WIN32)
        QVERIFY((order) == ((std::vector<HwBackend>{HwBackend::Cuda, HwBackend::D3D11VA, HwBackend::DXVA2})));
#else
        QVERIFY((order) == ((std::vector<HwBackend>{HwBackend::Cuda, HwBackend::VAAPI})));
#endif
    }

    void PreferenceListHasNoNoneAndNoDuplicates()
    {
        const auto order = PreferredHwBackends();
        for (const HwBackend b : order)
        {
            QVERIFY((b) != (HwBackend::None));
        }
        auto sorted = order;
        std::sort(sorted.begin(), sorted.end());
        QVERIFY((std::adjacent_find(sorted.begin(), sorted.end())) == (sorted.end()));
    }

    // ── HwBackendName ───────────────────────────────────────────────────────────────

    void BackendNamesMatchHwaccelStrings()
    {
        // Benchmark.cpp treats empty/"no"/"N/A" as software and shows "Hardware (<name>)"
        // otherwise — these names must be non-empty for every real backend.
        QVERIFY(::framelift::test::CStringEqual(HwBackendName(HwBackend::Vulkan), "vulkan"));
        QVERIFY(::framelift::test::CStringEqual(HwBackendName(HwBackend::Cuda), "cuda"));
        QVERIFY(::framelift::test::CStringEqual(HwBackendName(HwBackend::D3D11VA), "d3d11va"));
        QVERIFY(::framelift::test::CStringEqual(HwBackendName(HwBackend::DXVA2), "dxva2"));
        QVERIFY(::framelift::test::CStringEqual(HwBackendName(HwBackend::VAAPI), "vaapi"));
    }

    void NoneHasEmptyName()
    {
        QVERIFY(::framelift::test::CStringEqual(HwBackendName(HwBackend::None), ""));
    }

    void EveryPreferredBackendHasANonEmptyName()
    {
        for (const HwBackend b : PreferredHwBackends())
        {
            QVERIFY(::framelift::test::CStringNotEqual(HwBackendName(b), ""));
        }
    }

    void VideoDecodeModeNamesRoundTrip()
    {
        for (const VideoDecodeMode mode :
             {VideoDecodeMode::Off, VideoDecodeMode::Auto, VideoDecodeMode::VulkanZeroCopy, VideoDecodeMode::Vulkan,
              VideoDecodeMode::Cuda, VideoDecodeMode::D3D11VA, VideoDecodeMode::DXVA2, VideoDecodeMode::VAAPI})
        {
            QVERIFY((VideoDecodeModeFromString(VideoDecodeModeName(mode))) == (mode));
        }
    }

    void InvalidVideoDecodeModeDefaultsToAuto()
    {
        QVERIFY((VideoDecodeModeFromString("bogus")) == (VideoDecodeMode::Auto));
    }

    void PlainDecodeModesMapToReadbackBackends()
    {
        QVERIFY((HwBackendFromVideoDecodeMode(VideoDecodeMode::Vulkan)) == (HwBackend::Vulkan));
        QVERIFY((HwBackendFromVideoDecodeMode(VideoDecodeMode::Cuda)) == (HwBackend::Cuda));
        QVERIFY((HwBackendFromVideoDecodeMode(VideoDecodeMode::D3D11VA)) == (HwBackend::D3D11VA));
        QVERIFY((HwBackendFromVideoDecodeMode(VideoDecodeMode::DXVA2)) == (HwBackend::DXVA2));
        QVERIFY((HwBackendFromVideoDecodeMode(VideoDecodeMode::VAAPI)) == (HwBackend::VAAPI));
        QVERIFY((HwBackendFromVideoDecodeMode(VideoDecodeMode::VulkanZeroCopy)) == (HwBackend::None));
    }

    void AutoModePrefersGpuResidentModesBeforeReadback()
    {
        const auto order = AutoVideoDecodePreference();
        QVERIFY((order.size()) >= (3u));
        QVERIFY((order[0]) == (VideoDecodeMode::VulkanZeroCopy));
        QVERIFY((order[1]) == (VideoDecodeMode::Cuda));
    }

    // ── CandidateVideoDecodeModes (menu list before availability probing) ─────────

    void CandidateModesLeadWithOffThenAuto()
    {
        const auto modes = CandidateVideoDecodeModes();
        QVERIFY((modes.size()) >= (2u));
        QVERIFY((modes[0]) == (VideoDecodeMode::Off));
        QVERIFY((modes[1]) == (VideoDecodeMode::Auto));
    }

    void CandidateModesOmitForeignPlatformBackends()
    {
        const auto modes = CandidateVideoDecodeModes();
        const auto has = [&](VideoDecodeMode m)
        {
            return std::ranges::find(modes, m) != modes.end();
        };
        // Cross-vendor readback backends are platform-specific — the list must never
        // offer the other platform's native backend (the reported "d3d11va on Linux").
#if defined(_WIN32)
        QVERIFY(has(VideoDecodeMode::D3D11VA));
        QVERIFY(has(VideoDecodeMode::DXVA2));
        QVERIFY(!has(VideoDecodeMode::VAAPI));
#else
        QVERIFY(has(VideoDecodeMode::VAAPI));
        QVERIFY(!has(VideoDecodeMode::D3D11VA));
        QVERIFY(!has(VideoDecodeMode::DXVA2));
#endif
        // Cuda is a candidate everywhere (availability is decided later by the probe).
        QVERIFY(has(VideoDecodeMode::Cuda));
    }

    void CandidateModesHonourVulkanBuildFlag()
    {
        const auto modes = CandidateVideoDecodeModes();
        const auto has = [&](VideoDecodeMode m)
        {
            return std::ranges::find(modes, m) != modes.end();
        };
#if FRAMELIFT_MODULE_GRAPHICS_VULKAN
        QVERIFY(has(VideoDecodeMode::VulkanZeroCopy));
        QVERIFY(has(VideoDecodeMode::Vulkan));
#else
        QVERIFY(!has(VideoDecodeMode::VulkanZeroCopy));
        QVERIFY(!has(VideoDecodeMode::Vulkan));
#endif
    }

    // ── HwBackendForProbe (which device gates a mode) ─────────────────────────────

    void ProbeMappingSendsZeroCopyVariantsToTheirBaseDevice()
    {
        // Unlike HwBackendFromVideoDecodeMode, the zero-copy variant maps to a real
        // backend so one av_hwdevice probe decides whether to offer it.
        QVERIFY((HwBackendForProbe(VideoDecodeMode::Cuda)) == (HwBackend::Cuda));
        QVERIFY((HwBackendForProbe(VideoDecodeMode::D3D11VA)) == (HwBackend::D3D11VA));
        QVERIFY((HwBackendForProbe(VideoDecodeMode::DXVA2)) == (HwBackend::DXVA2));
        QVERIFY((HwBackendForProbe(VideoDecodeMode::VAAPI)) == (HwBackend::VAAPI));
#if FRAMELIFT_MODULE_GRAPHICS_VULKAN
        QVERIFY((HwBackendForProbe(VideoDecodeMode::VulkanZeroCopy)) == (HwBackend::Vulkan));
        QVERIFY((HwBackendForProbe(VideoDecodeMode::Vulkan)) == (HwBackend::Vulkan));
#endif
    }

    void ProbeMappingHasNoDeviceForOffAndAuto()
    {
        QVERIFY((HwBackendForProbe(VideoDecodeMode::Off)) == (HwBackend::None));
        QVERIFY((HwBackendForProbe(VideoDecodeMode::Auto)) == (HwBackend::None));
    }

    // ── IsKnownDecodeModeToken (reject a bogus FL_ACCEL_MODE) ─────────────────────

    void KnownTokensAreAccepted()
    {
        for (const char* token :
             {"off", "none", "software", "auto", "cuda", "nvdec", "cuvid", "d3d11va", "dxva2", "vaapi", "CUDA", "Auto"})
        {
            QVERIFY(IsKnownDecodeModeToken(token));
        }
#if FRAMELIFT_MODULE_GRAPHICS_VULKAN
        QVERIFY(IsKnownDecodeModeToken("vulkan"));
        QVERIFY(IsKnownDecodeModeToken("vk"));
        QVERIFY(IsKnownDecodeModeToken("vulkan-zero-copy"));
#endif
    }

    void GarbageTokensAreRejected()
    {
        QVERIFY(!IsKnownDecodeModeToken("bogus"));
        QVERIFY(!IsKnownDecodeModeToken(""));
        QVERIFY(!IsKnownDecodeModeToken("nvidia"));
    }
};

namespace
{
const ::framelift::test::Registrar<FFmpegHwDecodeTests> kRegisterFFmpegHwDecodeTests{"FFmpegHwDecodeTests"};
}

#include "FFmpegHwDecodeTests.moc"
