#include "FFmpegHwDecode.h"

#include <framelift/Log.h>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
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
                return *p;
            }
        }
        Log::Warn("FFmpegHwDecode: decoder did not offer the {} surface format; using software",
                  self->DeviceName());
    }
    return avcodec_default_get_format(ctx, fmts);
}
} // namespace

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

bool FFmpegHwDecode::TryEnableBackend(const AVCodec* codec, AVCodecContext* dec, HwBackend backend)
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
    const int err = av_hwdevice_ctx_create(&device, type, nullptr, nullptr, 0);
    if (err < 0 || !device)
    {
        return false; // device unavailable (no driver / no GPU)
    }

    device_ = device;
    hwPixFmt_ = pixFmt;
    deviceName_ = HwBackendName(backend);
    dec->hw_device_ctx = av_buffer_ref(device_);
    dec->opaque = this;
    dec->get_format = GetFormatCb;
    Log::Info("FFmpegHwDecode: hardware decode via {}", deviceName_);
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
    Log::Info("FFmpegHwDecode: zero-copy Vulkan video decode");
    return true;
}

bool FFmpegHwDecode::TryEnableCudaZeroCopy(const AVCodec* codec, AVCodecContext* dec, bool warn)
{
    (void)codec;
    (void)dec;
    if (warn)
    {
        Log::Warn("FFmpegHwDecode: CUDA zero-copy/no-readback renderer interop is not available in this build");
    }
    return false;
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
