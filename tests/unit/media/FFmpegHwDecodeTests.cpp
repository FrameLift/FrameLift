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
              VideoDecodeMode::CudaZeroCopy, VideoDecodeMode::Cuda, VideoDecodeMode::D3D11VA, VideoDecodeMode::DXVA2,
              VideoDecodeMode::VAAPI})
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
        QVERIFY((HwBackendFromVideoDecodeMode(VideoDecodeMode::CudaZeroCopy)) == (HwBackend::None));
    }

    void AutoModePrefersGpuResidentModesBeforeReadback()
    {
        const auto order = AutoVideoDecodePreference();
        QVERIFY((order.size()) >= (4u));
        QVERIFY((order[0]) == (VideoDecodeMode::VulkanZeroCopy));
        QVERIFY((order[1]) == (VideoDecodeMode::CudaZeroCopy));
        QVERIFY((order[2]) == (VideoDecodeMode::Cuda));
    }
};

namespace
{
const ::framelift::test::Registrar<FFmpegHwDecodeTests> kRegisterFFmpegHwDecodeTests{"FFmpegHwDecodeTests"};
}

#include "FFmpegHwDecodeTests.moc"
