#pragma once

#if defined(_WIN32)
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#elif defined(__linux__)
#ifndef VK_USE_PLATFORM_WAYLAND_KHR
#define VK_USE_PLATFORM_WAYLAND_KHR
#endif
#ifndef VK_USE_PLATFORM_XCB_KHR
#define VK_USE_PLATFORM_XCB_KHR
#endif
#endif

#include <vulkan/vulkan.h>

#include <QtCore/QString>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "FFmpegVulkanDevice.h"
#include "IGraphicsBackend.h"
#include "VulkanDeviceInfo.h"
#include "VulkanFrameBridgeState.h"
#include "VulkanQueueLock.h"
#include "VulkanRetireQueue.h"
#include "VulkanTimelineSignalState.h"

typedef struct VmaAllocator_T* VmaAllocator;

class QQuickWindow;
class QVulkanInstance;
class QWindow;

// FrameLift owns the Vulkan instance/device so the enabled feature chain and queues
// are suitable for FFmpeg zero-copy. Qt Quick adopts those objects and remains the
// sole owner of the surface, swapchain, render targets, frame synchronization, and
// presentation.
//
// Error policy (macros in VulkanUtil.h): construction/device setup throws — which
// IsSupported() converts into a clean "unsupported" probe result; renderer init paths
// return false and log; per-frame hot paths log once per call site and carry on.
class VulkanGraphicsBackend final : public IGraphicsBackend
{
public:
    static constexpr uint32_t kMaxFramesInFlight = 4;

    VulkanGraphicsBackend();
    ~VulkanGraphicsBackend() override;

    static bool IsSupported();

    void ConfigureQtWindow(QQuickWindow* window) override;
    void OnQtWindowCreated(QQuickWindow* window) override;
    void PrepareQtFrame(QQuickWindow* window) override;
    void Shutdown() override;

    [[nodiscard]] const char* Name() const override
    {
        return "Vulkan";
    }

    [[nodiscard]] bool HasNvidiaAdapter() const noexcept override
    {
        return nvidiaAdapter_;
    }

    [[nodiscard]] std::unique_ptr<IVideoRenderer> CreateVideoRenderer() override;

    [[nodiscard]] void* GetProcAddr(const char* name) const override;
    [[nodiscard]] bool GetVulkanDeviceInfo(VulkanDeviceInfo& out) const noexcept override;

    [[nodiscard]] VkDevice Device() const
    {
        return device_;
    }

    [[nodiscard]] VkPhysicalDevice PhysicalDevice() const
    {
        return physicalDevice_;
    }

    [[nodiscard]] VmaAllocator Allocator() const
    {
        return allocator_;
    }

    // Persisted across runs (disk-backed): removes the pipeline-rebuild hitch on YCbCr
    // format changes and first frames. VK_NULL_HANDLE is a valid fallback everywhere.
    [[nodiscard]] VkPipelineCache PipelineCache() const
    {
        return pipelineCache_;
    }

    [[nodiscard]] VkRenderPass RenderPass() const
    {
        return renderPass_;
    }

    [[nodiscard]] VkExtent2D SwapchainExtent() const
    {
        return frameExtent_;
    }

    [[nodiscard]] VkCommandBuffer CurrentCommandBuffer() const
    {
        return currentCmd_;
    }

    [[nodiscard]] uint32_t CurrentFrameIndex() const
    {
        return currentFrameSlot_;
    }

    [[nodiscard]] VkQueue GraphicsQueue() const
    {
        return graphicsQueue_;
    }

    [[nodiscard]] uint32_t GraphicsQueueFamily() const
    {
        return graphicsQueueFamily_;
    }

    [[nodiscard]] bool SupportsVulkanVideoDecode() const
    {
        return supportsVulkanVideo_;
    }

    [[nodiscard]] bool IsRecordingVideoLayer() const noexcept
    {
        return currentCmd_ != VK_NULL_HANDLE;
    }

    [[nodiscard]] VulkanQueueLock& QueueLock()
    {
        return queueLock_;
    }

    // Start tracking a decoder frames pool. Timeline histories are kept independently
    // while an older pool still has a bridge slot in flight.
    void BeginFrameSignalPool(uint64_t framesContextId);

    // True while the frame is locked awaiting Qt's submission callback. Used to make
    // repeated render-node calls in one Qt frame a no-op instead of a recursive lock.
    [[nodiscard]] bool HasPendingFrameSignal(void* frameIdentity) const;

    // Reserve and queue the frame's next timeline value while taking ownership of its
    // lock. The decoder wait is converted to a binary semaphore on the isolated bridge
    // queue; Qt consumes that binary in its own graphics submission.
    bool QueueFrameSignal(VkSemaphore semaphore, uint64_t requestedValue, LockedVulkanFrame&& frame);

    // Install the binary wait/signal pair on the current QRhi submission. Called from
    // QSGRenderNode::prepare(), after all native barriers have been recorded.
    bool InstallFrameBridge();

    // Abandon a reservation before Qt's queue submit parameters are installed.
    void CancelFrameBridge();

    [[nodiscard]] bool FrameBridgeInstalled() const noexcept;

    // Frame counter driving the retire queue; also used by per-slot staging arenas to
    // detect their first use in a new frame.
    [[nodiscard]] uint64_t FrameCounter() const
    {
        return retireQueue_.CurrentFrame();
    }

    // ── Host image copy (Vulkan 1.4 core / VK_EXT_host_image_copy on 1.3) ──────────
    // CPU-decoded frames copy straight from decoder memory into the sampled image with
    // no staging buffer, no barriers and no queue submit on the render thread. Gated on
    // device support and R8G8B8A8 host-transfer format support; selected by default
    // only on integrated/UMA adapters — on discrete GPUs the driver's CPU-side
    // detiling makes it measurably slower than the staging path (see
    // SetupHostImageCopy). FL_VK_HOST_COPY=1/0 forces it on/off.
    [[nodiscard]] bool SupportsHostImageCopy() const
    {
        return hostImageCopy_;
    }

    // Layout host-copied sampled images live in (SHADER_READ_ONLY_OPTIMAL when the
    // implementation lists it as a copy-dst layout, else GENERAL).
    [[nodiscard]] VkImageLayout HostCopyDstLayout() const
    {
        return hostCopyDstLayout_;
    }

    bool HostTransitionImage(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);
    bool HostCopyToImage(VkImage image, VkImageLayout layout, const void* pixels, uint32_t w, uint32_t h);

    // ── Push descriptors (Vulkan 1.4 core / VK_KHR_push_descriptor on 1.3) ─────────
    // Per-frame YCbCr image bindings push straight into the command buffer instead of
    // allocating descriptor sets from a pool (whose exhaustion/invalidation dance they
    // otherwise need). FL_VK_NO_PUSH_DESC=1 forces the pool path for testing.
    [[nodiscard]] bool SupportsPushDescriptors() const
    {
        return pushDescriptors_;
    }

    void CmdPushDescriptorSet(VkCommandBuffer cmd, VkPipelineLayout layout, const VkWriteDescriptorSet& write);

    // Deferred destruction for objects frames in flight may still reference; collected
    // once per prepared frame (PrepareQtFrame), drained on idle teardown paths. Replaces
    // mid-frame vkDeviceWaitIdle stalls on resize / format change / pool swap.
    void Retire(std::function<void()> destroy)
    {
        retireQueue_.Retire(std::move(destroy));
    }

    // Run all retired destructors now. Caller must guarantee the device is idle.
    void DrainRetired()
    {
        retireQueue_.Drain();
    }

private:
    struct FrameBridgeSlot;

    void CreateInstance();
    void SetupDebugUtils();
    void CreateDevice(QWindow* presentProbe);
    [[nodiscard]] bool HostTransferFormatSupported() const;
    void SetupHostImageCopy(bool selected);
    void DetectVideoDecodeQueue(const std::vector<VkQueueFamilyProperties>& queueProperties);
    void SetupFrameBridge();
    void RefreshQtResources(QQuickWindow* window);
    void AcquireFrameBridgeSlot();
    void FlushFrameSignals();
    void CompleteFrameSignals(FrameBridgeSlot& slot, bool hostSignal);
    void UnlockFrameSignals(FrameBridgeSlot& slot);
    bool RecreateFrameBridgeBinaries(FrameBridgeSlot& slot);
    void DrainFrameBridgeAfterQtStopped();
    void DestroyDevice();

    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue videoDecodeQueue_ = VK_NULL_HANDLE;
    VkQueue bridgeQueue_ = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamily_ = 0;
    uint32_t qtGraphicsQueueIndex_ = 0;
    int videoDecodeQueueFamily_ = -1;
    int bridgeQueueFamily_ = -1;
    uint32_t bridgeQueueIndex_ = 0;
    uint32_t graphicsQueueFlags_ = 0;
    uint32_t videoDecodeQueueFlags_ = 0;
    uint32_t videoDecodeCaps_ = 0;
    uint32_t instanceApiVersion_ = VK_API_VERSION_1_3;
    uint32_t deviceApiVersion_ = VK_API_VERSION_1_3;

    bool nvidiaAdapter_ = false;
    bool supportsVulkanVideo_ = false;
    bool configured_ = false;
    bool shutdown_ = false;
    bool validationActive_ = false;
    bool hostImageCopy_ = false;
    VkImageLayout hostCopyDstLayout_ = VK_IMAGE_LAYOUT_GENERAL;
    PFN_vkTransitionImageLayoutEXT transitionImageLayoutFn_ = nullptr;
    PFN_vkCopyMemoryToImageEXT copyMemoryToImageFn_ = nullptr;
    bool pushDescriptors_ = false;
    PFN_vkCmdPushDescriptorSetKHR pushDescriptorSetFn_ = nullptr;

    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
    PFN_vkDestroyDebugUtilsMessengerEXT destroyDebugMessengerFn_ = nullptr;

    void LoadPipelineCache();
    void SavePipelineCache();
    [[nodiscard]] static QString PipelineCachePath();

    std::unique_ptr<QVulkanInstance> qtInstance_;
    VkPipelineCache pipelineCache_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = nullptr;
    VulkanQueueLock queueLock_;
    VulkanRetireQueue retireQueue_;
    QQuickWindow* window_ = nullptr; // non-owning; cleared when the scene graph stops

    VkRenderPass renderPass_ = VK_NULL_HANDLE;    // Qt-owned, current frame
    VkCommandBuffer currentCmd_ = VK_NULL_HANDLE; // Qt-owned, current frame
    VkExtent2D frameExtent_{};
    uint32_t currentFrameSlot_ = 0;

    struct TimelineSignal
    {
        uint64_t poolId = 0;
        VkSemaphore semaphore = VK_NULL_HANDLE;
        uint64_t waitValue = 0;
        uint64_t value = 0;
        std::vector<LockedVulkanFrame> frames;
    };

    struct FrameBridgeSlot
    {
        VulkanFrameBridge::Phase phase = VulkanFrameBridge::Phase::Available;
        uint64_t frameSerial = UINT64_MAX;
        VkSemaphore ready = VK_NULL_HANDLE;
        VkSemaphore done = VK_NULL_HANDLE;
        VkFence completionFence = VK_NULL_HANDLE;
        std::vector<TimelineSignal> timelineSignals;
    };

    std::unordered_map<uint64_t, VulkanTimelineSignalState> frameSignalStates_;
    std::array<FrameBridgeSlot, kMaxFramesInFlight> frameBridgeSlots_{};
    FrameBridgeSlot* activeFrameBridgeSlot_ = nullptr;
    std::vector<VkSemaphoreSubmitInfo> frameBridgeWaitScratch_;
    std::vector<VkSemaphoreSubmitInfo> frameBridgeSignalScratch_;

    std::vector<std::string> instanceExtNames_;
    std::vector<std::string> enabledDeviceExtNames_;
    std::vector<const char*> enabledDeviceExtPtrs_;

    VkPhysicalDeviceFeatures2 enabledFeatures2_{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    VkPhysicalDeviceVulkan11Features enabledF11_{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    VkPhysicalDeviceVulkan12Features enabledF12_{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan13Features enabledF13_{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
#ifdef VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES
    VkPhysicalDeviceVulkan14Features enabledF14_{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES};
#endif
    // Chained into the device create only on a 1.3 device using VK_EXT_host_image_copy
    // (on 1.4 the core feature bit in enabledF14_ is used instead). Must outlive the
    // device: the features chain is also handed to FFmpeg via GetVulkanDeviceInfo.
    VkPhysicalDeviceHostImageCopyFeaturesEXT enabledHostCopy_{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_FEATURES_EXT
    };
};
