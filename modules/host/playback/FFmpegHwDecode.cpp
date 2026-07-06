#include "FFmpegHwDecode.h"

#include <framelift/Log.h>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/log.h>
#include <libavutil/pixdesc.h>
#if FRAMELIFT_MODULE_GRAPHICS_VULKAN
#include <libavutil/hwcontext_vulkan.h> // AVVulkanFramesContext (decode-pool usage tweak)
#endif
}

namespace
{
// Map our platform-neutral backend enum to the libav device type.
AVHWDeviceType ToDeviceType(HwBackend backend)
{
    switch (backend)
    {
    case HwBackend::Vulkan:
        return AV_HWDEVICE_TYPE_VULKAN;
    case HwBackend::Cuda:
        return AV_HWDEVICE_TYPE_CUDA;
    case HwBackend::D3D11VA:
        return AV_HWDEVICE_TYPE_D3D11VA;
    case HwBackend::DXVA2:
        return AV_HWDEVICE_TYPE_DXVA2;
    case HwBackend::VAAPI:
        return AV_HWDEVICE_TYPE_VAAPI;
    case HwBackend::None:
        break;
    }
    return AV_HWDEVICE_TYPE_NONE;
}

#if FRAMELIFT_MODULE_GRAPHICS_VULKAN
// Allocate the Vulkan decode frame pool ourselves instead of letting avcodec do it,
// solely to trim the pool images' create parameters down to what the driver's
// vkGetPhysicalDeviceVideoFormatPropertiesKHR actually advertises for the decode
// profile — FFmpeg's defaults exceed it, making every pool vkCreateImage a
// validation error (VUID-VkImageCreateInfo-pNext-06811). On NVIDIA the advertised
// combination is TRANSFER|SAMPLED|DECODE_DST|DECODE_DPB usage with
// MUTABLE_FORMAT|EXTENDED_USAGE create flags, while FFmpeg additionally requests
// STORAGE usage and the ALIAS create flag. Neither is needed here: Vulkan video
// decode writes via DECODE_DST/DPB, the renderer only samples, and the pool binds
// one multiplane image per frame (nothing aliases memory). Everything else
// (avcodec_get_hw_frames_parameters) is exactly what avcodec would have set up —
// including the multiplane layout GetVulkanFrameInfo depends on. Returns false on
// any failure, in which case the caller leaves hw_frames_ctx unset and avcodec
// allocates the pool itself (today's behavior, validation warts and all).
bool SetupVulkanFramesPool(AVCodecContext* ctx)
{
    if (ctx->hw_frames_ctx || !ctx->hw_device_ctx)
    {
        return false;
    }
    AVBufferRef* frames = nullptr;
    if (avcodec_get_hw_frames_parameters(ctx, ctx->hw_device_ctx, AV_PIX_FMT_VULKAN, &frames) < 0 || !frames)
    {
        return false;
    }
    auto* fc = reinterpret_cast<AVHWFramesContext*>(frames->data);
    auto* vfc = static_cast<AVVulkanFramesContext*>(fc->hwctx);
    // AVVulkanFramesContext declares `usage` as the enum type, hence the cast dance.
    vfc->usage = static_cast<VkImageUsageFlagBits>(vfc->usage & ~VK_IMAGE_USAGE_STORAGE_BIT);
    // img_flags is 0 here and av_hwframe_ctx_init would default it to ALIAS (see
    // hwcontext_vulkan.h); set it explicitly to the flags multiplane sampling needs
    // (and the driver advertises) so ALIAS never lands in vkCreateImage.
    vfc->img_flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_EXTENDED_USAGE_BIT;
    if (av_hwframe_ctx_init(frames) < 0)
    {
        av_buffer_unref(&frames);
        return false;
    }
    ctx->hw_frames_ctx = frames;
    return true;
}
#endif

// dec->get_format: pick the hardware surface format when the decoder offers it,
// otherwise fall back to libav's default (software). Set once before avcodec_open2
// and never mutated, so the decode-thread callback needs no locking. A file-local
// free function (not a member) so the header stays libav-free / test-includable;
// it reaches the instance via ctx->opaque and only needs the public getters.
enum AVPixelFormat GetFormatCb(AVCodecContext* ctx, const enum AVPixelFormat* fmts)
{
    const auto* self = static_cast<const FFmpegHwDecode*>(ctx->opaque);
    if (self)
    {
        for (const enum AVPixelFormat* p = fmts; *p != AV_PIX_FMT_NONE; ++p)
        {
            if (static_cast<int>(*p) == self->HwPixelFormat())
            {
#if FRAMELIFT_MODULE_GRAPHICS_VULKAN
                if (*p == AV_PIX_FMT_VULKAN && !SetupVulkanFramesPool(ctx))
                {
                    Log::Debug("FFmpegHwDecode: custom Vulkan frame pool unavailable; using avcodec's");
                }
#endif
                return *p;
            }
        }
        Log::Warn("FFmpegHwDecode: decoder did not offer the {} surface format; using software", self->DeviceName());
    }
    return avcodec_default_get_format(ctx, fmts);
}
} // namespace

bool ProbeHwBackendAvailable(HwBackend backend)
{
    const AVHWDeviceType type = ToDeviceType(backend);
    if (type == AV_HWDEVICE_TYPE_NONE)
    {
        return false;
    }
    AVBufferRef* device = nullptr;
    // Silence libav's own error spam for an *expected* probe miss (e.g. "Failed to
    // initialise VAAPI connection" on a machine without that driver) — a failed
    // create here is a normal answer, not an error worth surfacing to the user.
    // FFmpegLogCallback gates on av_log_get_level(), so lowering it drops the message.
    // The level is global libav state but the probe runs on the caller thread and is
    // restored immediately; a few ms of quiet is harmless to any concurrent decoder.
    const int prevLevel = av_log_get_level();
    av_log_set_level(AV_LOG_QUIET);
    const int err = av_hwdevice_ctx_create(&device, type, nullptr, nullptr, 0);
    av_log_set_level(prevLevel);
    if (err < 0 || !device)
    {
        return false; // device unavailable (no driver / no GPU / wrong vendor)
    }
    av_buffer_unref(&device);
    return true;
}

FFmpegHwDecode::~FFmpegHwDecode()
{
    av_buffer_unref(&device_);
}

bool FFmpegHwDecode::TryEnable(const AVCodec* codec, AVCodecContext* dec)
{
    if (!codec || !dec)
    {
        return false;
    }

    for (const HwBackend backend : PreferredHwBackends())
    {
        if (TryEnableBackend(codec, dec, backend))
        {
            return true;
        }
    }

    return false; // no backend available — caller decodes in software
}

bool FFmpegHwDecode::TryEnableBackend(
    const AVCodec* codec, AVCodecContext* dec, HwBackend backend, HwDeviceCache* cache
)
{
    if (!codec || !dec)
    {
        return false;
    }

    const AVHWDeviceType type = ToDeviceType(backend);
    if (type == AV_HWDEVICE_TYPE_NONE)
    {
        return false;
    }

    // Does this codec advertise a hw-device-ctx config for this device type?
    AVPixelFormat pixFmt = AV_PIX_FMT_NONE;
    for (int i = 0;; ++i)
    {
        const AVCodecHWConfig* cfg = avcodec_get_hw_config(codec, i);
        if (!cfg)
        {
            break;
        }
        if ((cfg->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) != 0 && cfg->device_type == type)
        {
            pixFmt = cfg->pix_fmt;
            break;
        }
    }
    if (pixFmt == AV_PIX_FMT_NONE)
    {
        return false; // codec can't use this backend
    }

    AVBufferRef* device = nullptr;
    if (cache && cache->device && cache->type == static_cast<int>(type))
    {
        device = av_buffer_ref(cache->device); // reuse the previous file's device
        if (!device)
        {
            return false;
        }
    }
    else
    {
        const int err = av_hwdevice_ctx_create(&device, type, nullptr, nullptr, 0);
        if (err < 0 || !device)
        {
            return false; // device unavailable (no driver / no GPU)
        }
        if (cache)
        {
            av_buffer_unref(&cache->device); // a different backend was cached
            cache->device = av_buffer_ref(device);
            cache->type = static_cast<int>(type);
        }
    }

    device_ = device;
    hwPixFmt_ = pixFmt;
    deviceName_ = HwBackendName(backend);
    dec->hw_device_ctx = av_buffer_ref(device_);
    dec->opaque = this;
    dec->get_format = GetFormatCb;
    Log::Debug("FFmpegHwDecode: hardware decode via {}", deviceName_);
    return true;
}

bool FFmpegHwDecode::TryEnableVulkan(const AVCodec* codec, AVCodecContext* dec, AVBufferRef* vkDevice)
{
    if (!codec || !dec || !vkDevice)
    {
        return false;
    }

    // Does this codec advertise a Vulkan hw-device-ctx decode config?
    AVPixelFormat pixFmt = AV_PIX_FMT_NONE;
    for (int i = 0;; ++i)
    {
        const AVCodecHWConfig* cfg = avcodec_get_hw_config(codec, i);
        if (!cfg)
        {
            break;
        }
        if ((cfg->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) != 0 &&
            cfg->device_type == AV_HWDEVICE_TYPE_VULKAN)
        {
            pixFmt = cfg->pix_fmt; // AV_PIX_FMT_VULKAN
            break;
        }
    }
    if (pixFmt == AV_PIX_FMT_NONE)
    {
        return false; // codec can't be Vulkan-decoded — caller falls back
    }

    // Wrap the renderer's device (one ref kept here for cleanup, one for the decoder).
    // FFmpeg auto-allocates the AVVkFrame pool (multiplane by default) when get_format
    // returns AV_PIX_FMT_VULKAN.
    device_ = av_buffer_ref(vkDevice);
    hwPixFmt_ = pixFmt;
    deviceName_ = "vulkan-zero-copy";
    isVulkanZeroCopy_ = true;
    dec->hw_device_ctx = av_buffer_ref(device_);
    dec->opaque = this;
    dec->get_format = GetFormatCb;
    Log::Debug("FFmpegHwDecode: zero-copy Vulkan video decode");
    return true;
}

AVFrame* FFmpegHwDecode::MapToSoftware(AVFrame* src, AVFrame* dst)
{
    if (!src || src->format != hwPixFmt_)
    {
        return src; // already a software frame
    }
    av_frame_unref(dst);
    if (av_hwframe_transfer_data(dst, src, 0) < 0)
    {
        Log::Warn("FFmpegHwDecode: hw frame download failed; skipping frame");
        return nullptr;
    }
    // transfer copies pixels only — pts/time-base live in the props.
    av_frame_copy_props(dst, src);
    return dst;
}
