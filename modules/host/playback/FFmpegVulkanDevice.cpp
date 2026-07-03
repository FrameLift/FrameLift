#include "FFmpegVulkanDevice.h"

#include "VulkanQueueLock.h"

#include <framelift/Log.h>

#include <cstdint>

extern "C"
{
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_vulkan.h>
}

// Scoped suppression of deprecated-field writes (cross-compiler).
#if defined(__GNUC__) || defined(__clang__)
#define FFVK_PUSH_IGNORE_DEPRECATED                                                                                    \
    _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"")
#define FFVK_POP_IGNORE_DEPRECATED _Pragma("GCC diagnostic pop")
#elif defined(_MSC_VER)
#define FFVK_PUSH_IGNORE_DEPRECATED __pragma(warning(push)) __pragma(warning(disable : 4996))
#define FFVK_POP_IGNORE_DEPRECATED __pragma(warning(pop))
#else
#define FFVK_PUSH_IGNORE_DEPRECATED
#define FFVK_POP_IGNORE_DEPRECATED
#endif

namespace
{
// FFmpeg submits transfer/compute work from its decode thread on queues it shares with
// the renderer. These callbacks let it take the renderer's VulkanQueueLock (stashed in
// user_opaque) so those submits can't race the render thread's submits/presents.
void LockQueueCb(AVHWDeviceContext* ctx, uint32_t queueFamily, uint32_t index)
{
    if (auto* lock = static_cast<VulkanQueueLock*>(ctx->user_opaque))
    {
        lock->Lock(queueFamily, index);
    }
}

void UnlockQueueCb(AVHWDeviceContext* ctx, uint32_t queueFamily, uint32_t index)
{
    if (auto* lock = static_cast<VulkanQueueLock*>(ctx->user_opaque))
    {
        lock->Unlock(queueFamily, index);
    }
}
} // namespace

AVBufferRef* CreateVulkanHwDevice(const VulkanDeviceInfo& info)
{
    if (!info.device || !info.instance || !info.physicalDevice || !info.getInstanceProcAddr)
    {
        return nullptr;
    }

    AVBufferRef* ref = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_VULKAN);
    if (!ref)
    {
        return nullptr;
    }

    auto* devCtx = reinterpret_cast<AVHWDeviceContext*>(ref->data);
    auto* vk = static_cast<AVVulkanDeviceContext*>(devCtx->hwctx);

    // Serialize FFmpeg's queue submits against the renderer's: both touch the same,
    // non-thread-safe VkQueue. Without this, decode-thread submits race the render
    // thread's and hang the driver (issue #26). user_opaque carries the shared lock to
    // the callbacks; it is the user's field and untouched by av_hwdevice_ctx_init.
    devCtx->user_opaque = info.queueLock;
    if (info.queueLock && !info.internalQueueSync)
    {
        // Fallback path (no VK_KHR_internally_synchronized_queues support): FFmpeg's
        // lock_queue/unlock_queue are deprecated, but they are the only portable way to
        // make FFmpeg's decode-thread submits share the renderer's VulkanQueueLock when
        // both touch the same VkQueue (issue #26). When the device's queues are internally
        // synchronized the driver handles this, so these callbacks are left unset and no
        // deprecated field is touched at all. This is the ONLY deprecated use in the TU.
        FFVK_PUSH_IGNORE_DEPRECATED
        vk->lock_queue = LockQueueCb;
        vk->unlock_queue = UnlockQueueCb;
        FFVK_POP_IGNORE_DEPRECATED
    }

    vk->get_proc_addr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(info.getInstanceProcAddr);
    vk->inst = reinterpret_cast<VkInstance>(info.instance);
    vk->phys_dev = reinterpret_cast<VkPhysicalDevice>(info.physicalDevice);
    vk->act_dev = reinterpret_cast<VkDevice>(info.device);

    if (info.featuresChain)
    {
        // Copy the base struct; its pNext still points at the backend's 11/12/13 feature
        // structs, which outlive this device context.
        vk->device_features = *static_cast<const VkPhysicalDeviceFeatures2*>(info.featuresChain);
    }

    vk->enabled_inst_extensions = nullptr;
    vk->nb_enabled_inst_extensions = 0;
    vk->enabled_dev_extensions = info.deviceExtensions;
    vk->nb_enabled_dev_extensions = info.deviceExtensionCount;

    // Modern preferentially-ordered queue-family list. Graphics first (used for the
    // generic transfer/compute work FFmpeg may do), then the dedicated decode family.
    int n = 0;
    if (info.graphicsQueueFamily >= 0)
    {
        vk->qf[n].idx = info.graphicsQueueFamily;
        vk->qf[n].num = 1;
        vk->qf[n].flags = static_cast<VkQueueFlagBits>(info.graphicsQueueFlags);
        vk->qf[n].video_caps = info.graphicsQueueFamily == info.videoDecodeQueueFamily
                                   ? static_cast<VkVideoCodecOperationFlagBitsKHR>(info.videoDecodeCaps)
                                   : static_cast<VkVideoCodecOperationFlagBitsKHR>(0);
        ++n;
    }
    if (info.videoDecodeQueueFamily >= 0 && info.videoDecodeQueueFamily != info.graphicsQueueFamily)
    {
        vk->qf[n].idx = info.videoDecodeQueueFamily;
        vk->qf[n].num = 1;
        vk->qf[n].flags = static_cast<VkQueueFlagBits>(info.videoDecodeQueueFlags);
        vk->qf[n].video_caps = static_cast<VkVideoCodecOperationFlagBitsKHR>(info.videoDecodeCaps);
        ++n;
    }
    vk->nb_qf = n;

    const int err = av_hwdevice_ctx_init(ref);
    if (err < 0)
    {
        Log::Warn("FFmpegVulkanDevice: av_hwdevice_ctx_init failed ({})", err);
        av_buffer_unref(&ref);
        return nullptr;
    }
    return ref;
}

bool GetVulkanFrameInfo(void* avFrame, VulkanFrameInfo& out)
{
    auto* frame = static_cast<AVFrame*>(avFrame);
    if (!frame || frame->format != AV_PIX_FMT_VULKAN || !frame->hw_frames_ctx)
    {
        return false;
    }
    auto* vkf = reinterpret_cast<AVVkFrame*>(frame->data[0]);
    if (!vkf)
    {
        return false;
    }
    auto* fc = reinterpret_cast<AVHWFramesContext*>(frame->hw_frames_ctx->data);
    auto* vfc = static_cast<AVVulkanFramesContext*>(fc->hwctx);

    out.framesContextId = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(fc));
    vfc->lock_frame(fc, vkf);
    out.image = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(vkf->img[0]));
    out.semaphore = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(vkf->sem[0]));
    out.semValue = vkf->sem_value[0];
    out.layout = static_cast<int>(vkf->layout[0]);
    out.queueFamily = vkf->queue_family[0];
    vfc->unlock_frame(fc, vkf);

    out.vkFormat = static_cast<int>(vfc->format[0]);
    out.colorSpace = static_cast<int>(frame->colorspace);
    out.colorRange = static_cast<int>(frame->color_range);
    out.width = frame->width;
    out.height = frame->height;
    out.valid = true;

    // Multiplane single-image is required for the YCbCr sampler path; a second image
    // means the driver fell back to per-plane images, which we don't sample here.
    if (vkf->img[1] != VK_NULL_HANDLE)
    {
        out.valid = false;
        return false;
    }
    return true;
}

void SetVulkanFrameState(void* avFrame, int newLayout, unsigned long long newSemValue, unsigned int newQueueFamily)
{
    auto* frame = static_cast<AVFrame*>(avFrame);
    if (!frame || frame->format != AV_PIX_FMT_VULKAN || !frame->hw_frames_ctx)
    {
        return;
    }
    auto* vkf = reinterpret_cast<AVVkFrame*>(frame->data[0]);
    if (!vkf)
    {
        return;
    }
    auto* fc = reinterpret_cast<AVHWFramesContext*>(frame->hw_frames_ctx->data);
    auto* vfc = static_cast<AVVulkanFramesContext*>(fc->hwctx);

    vfc->lock_frame(fc, vkf);
    vkf->layout[0] = static_cast<VkImageLayout>(newLayout);
    vkf->access[0] = VK_ACCESS_SHADER_READ_BIT;
    vkf->sem_value[0] = newSemValue;
    vkf->queue_family[0] = newQueueFamily;
    vfc->unlock_frame(fc, vkf);
}
