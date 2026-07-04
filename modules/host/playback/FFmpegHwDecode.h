#pragma once

#include <vector>

struct AVCodec;
struct AVCodecContext;
struct AVFrame;
struct AVBufferRef;

// Hardware video decode for the FFmpeg backend (issue #25, Phase 7). Picks a GPU
// decode backend, attaches an AVHWDeviceContext to the decoder, and downloads hw
// frames to system memory so the existing swscale → RGBA path is unchanged. One of
// the few FFmpeg* files that may #include <libav*/...> (in the .cpp only).
//
// Decode-on-GPU + CPU readback is the default hardware path. Vulkan zero-copy can
// instead wrap the renderer's device and hand AVVkFrames directly to the renderer.
//
// Lifetime: owned by FFmpegPlayer::PlayFile and passed by pointer into VideoWorker.
// It must outlive the decoder it arms (the decoder frees through the device ctx), so
// declare it after the AVCodecContext* so it is destroyed last.

// Ordered list of hardware backends to try, most-preferred first. Pure (no libav) so
// it is unit-testable: NVIDIA nvdec leads on every platform, then the cross-vendor
// native backend (D3D11VA → DXVA2 on Windows, VAAPI on Linux).
enum class HwBackend : int
{
    None,
    Vulkan,  // zero-copy decode onto the renderer's device — AV_HWDEVICE_TYPE_VULKAN (#18)
    Cuda,    // NVIDIA nvdec/cuvid — AV_HWDEVICE_TYPE_CUDA
    D3D11VA, // Windows, cross-vendor — AV_HWDEVICE_TYPE_D3D11VA
    DXVA2,   // Windows legacy fallback — AV_HWDEVICE_TYPE_DXVA2
    VAAPI,   // Linux, cross-vendor — AV_HWDEVICE_TYPE_VAAPI
};

// Platform-specific preference order consulted by TryEnable. Pure / libav-free
// (inline so the standalone native test suite can exercise it without FFmpeg).
inline std::vector<HwBackend> PreferredHwBackends()
{
#if defined(_WIN32)
    return {HwBackend::Cuda, HwBackend::D3D11VA, HwBackend::DXVA2};
#else
    return {HwBackend::Cuda, HwBackend::VAAPI};
#endif
}

// True if a hardware device for `backend` can actually be created on this machine
// (driver present + GPU supports it). Creates and immediately discards the device
// via av_hwdevice_ctx_create — the same test TryEnableBackend relies on — so it
// reflects real availability, not just what FFmpeg was compiled with. Costs tens of
// ms on success; callers should cache. HwBackend::None returns false. Defined in the
// .cpp (touches libav); declared here (not inline) so the header stays libav-free.
bool ProbeHwBackendAvailable(HwBackend backend);

// Human-readable backend name (the conventional FFmpeg hwaccel strings the
// DebugOverlay/Benchmark plugins display); "" for None.
inline const char* HwBackendName(HwBackend backend)
{
    switch (backend)
    {
    case HwBackend::Vulkan:
        return "vulkan";
    case HwBackend::Cuda:
        return "cuda";
    case HwBackend::D3D11VA:
        return "d3d11va";
    case HwBackend::DXVA2:
        return "dxva2";
    case HwBackend::VAAPI:
        return "vaapi";
    case HwBackend::None:
        break;
    }
    return "";
}

// Cross-file cache for a created AVHWDeviceContext. av_hwdevice_ctx_create costs
// tens of milliseconds (driver/GPU context init) and the resulting AVBufferRef is
// refcounted and designed to be shared across sequential decoders, so the player
// keeps one alive between files instead of recreating it per open. Decode-thread
// owned; the owner unrefs `device` after the decode thread has joined. `type` is
// the AVHWDeviceType the device was created for (as int so this header stays
// libav-free); a backend change just re-creates and replaces the entry.
struct HwDeviceCache
{
    AVBufferRef* device = nullptr;
    int type = -1;
};

class FFmpegHwDecode
{
public:
    FFmpegHwDecode() = default;
    ~FFmpegHwDecode();

    FFmpegHwDecode(const FFmpegHwDecode&) = delete;
    FFmpegHwDecode& operator=(const FFmpegHwDecode&) = delete;

    // Arm `dec` (already alloc'd + parameters-copied, NOT yet opened) with the first
    // PreferredHwBackends() entry the codec advertises and whose device can be created.
    // On success sets dec->hw_device_ctx, dec->opaque (= this) and dec->get_format, then
    // returns true. On any failure frees partial state and leaves `dec` pristine so the
    // caller decodes in software. Never throws / blocks.
    bool TryEnable(const AVCodec* codec, AVCodecContext* dec);

    // Arm `dec` with exactly one hardware backend using the readback path. This is
    // used by explicit user-selected modes such as "Vulkan" or "CUDA". When `cache`
    // is given, a matching cached device is reused instead of created, and a newly
    // created one is stored back for the next file.
    bool TryEnableBackend(const AVCodec* codec, AVCodecContext* dec, HwBackend backend,
                          HwDeviceCache* cache = nullptr);

    // Zero-copy variant (#18): arm `dec` for AV_HWDEVICE_TYPE_VULKAN, WRAPPING the
    // renderer's already-created device (`vkDevice`, owned by the caller) so decoded
    // AVVkFrames live on the render device and never read back. Returns false (leaving
    // `dec` pristine) if the codec can't Vulkan-decode, so the caller tries the readback
    // backends or software. Decoded frames keep ->format == AV_PIX_FMT_VULKAN and must
    // NOT go through MapToSoftware.
    bool TryEnableVulkan(const AVCodec* codec, AVCodecContext* dec, AVBufferRef* vkDevice);

    // CUDA no-readback is a renderer interop path, not just a decoder flag. It is
    // deliberately separate from TryEnableBackend(Cuda), which downloads to CPU.
    bool TryEnableCudaZeroCopy(const AVCodec* codec, AVCodecContext* dec, bool warn);

    [[nodiscard]] bool Active() const
    {
        return device_ != nullptr;
    }

    // True when the active backend is the zero-copy Vulkan path (frames are AVVkFrames
    // handed to the Vulkan renderer, not downloaded).
    [[nodiscard]] bool IsVulkanZeroCopy() const
    {
        return isVulkanZeroCopy_;
    }

    // The negotiated hardware pixel format (AVPixelFormat as int), or AV_PIX_FMT_NONE.
    // Frames whose ->format equals this need MapToSoftware before swscale.
    [[nodiscard]] int HwPixelFormat() const
    {
        return hwPixFmt_;
    }

    // Active backend name ("cuda"/"d3d11va"/...) or "no" when software.
    [[nodiscard]] const char* DeviceName() const
    {
        return deviceName_;
    }

    // If `src` is a hardware frame, download it into `dst` (carrying pts/props) and
    // return `dst`; otherwise return `src` untouched. Returns nullptr on transfer
    // failure (caller skips the frame). `dst` is a caller-owned reusable scratch frame.
    AVFrame* MapToSoftware(AVFrame* src, AVFrame* dst);

private:
    AVBufferRef* device_ = nullptr;
    int hwPixFmt_ = -1; // AV_PIX_FMT_NONE
    const char* deviceName_ = "no";
    bool isVulkanZeroCopy_ = false;
};
