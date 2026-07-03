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

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "IGraphicsBackend.h"
#include "VulkanDeviceInfo.h"
#include "VulkanQueueLock.h"
#include "VulkanRetireQueue.h"

typedef struct VmaAllocator_T* VmaAllocator;

class QQuickWindow;
class QVulkanInstance;
class QWindow;

// FrameLift owns the Vulkan instance/device so the enabled feature chain and queues
// are suitable for FFmpeg zero-copy. Qt Quick adopts those objects and remains the
// sole owner of the surface, swapchain, render targets, frame synchronization, and
// presentation.
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

    [[nodiscard]] uintptr_t CreateUITexture(const unsigned char*, int, int) override
    {
        return 0;
    }

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

    bool SubmitFrameTransition(VkCommandBuffer cmd, VkSemaphore waitSemaphore, uint64_t waitValue);
    void QueueFrameSignal(VkSemaphore semaphore, uint64_t value);

    // Last-resort delivery of queued-but-unsubmitted frame signals: waits for the device
    // to go idle, then host-signals each pending timeline value (vkSignalSemaphore).
    // The renderer already published value+1 into the AVVkFrame via SetVulkanFrameState,
    // so dropping a pending signal would leave FFmpeg waiting on it forever when it frees
    // or reuses the frame. Idle-first makes the host signal semantically correct: all GPU
    // reads of the frame have completed. Called from ~VulkanVideoRenderer (while the
    // AVVkFrame semaphores are still alive — the player destroys the renderer before
    // releasing its frame refs) and from Shutdown().
    void HostSignalPendingFrameSignals();

    bool ImmediateSubmit(void (*record)(VkCommandBuffer cmd, void* ud), void* ud);

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
    void CreateInstance();
    void SetupDebugUtils();
    void CreateDevice(QWindow* presentProbe);
    void DetectVideoDecodeQueue(const std::vector<VkQueueFamilyProperties>& queueProperties);
    void RefreshQtResources(QQuickWindow* window);
    void FlushFrameSignals();
    void DestroyDevice();

    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue videoDecodeQueue_ = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamily_ = 0;
    uint32_t qtGraphicsQueueIndex_ = 0;
    int videoDecodeQueueFamily_ = -1;
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

    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
    PFN_vkDestroyDebugUtilsMessengerEXT destroyDebugMessengerFn_ = nullptr;

    std::unique_ptr<QVulkanInstance> qtInstance_;
    VmaAllocator allocator_ = nullptr;
    VulkanQueueLock queueLock_;
    VulkanRetireQueue retireQueue_;

    VkCommandPool immediatePool_ = VK_NULL_HANDLE;
    VkCommandBuffer immediateCmd_ = VK_NULL_HANDLE;
    VkFence immediateFence_ = VK_NULL_HANDLE;

    VkRenderPass renderPass_ = VK_NULL_HANDLE;    // Qt-owned, current frame
    VkCommandBuffer currentCmd_ = VK_NULL_HANDLE; // Qt-owned, current frame
    VkExtent2D frameExtent_{};
    uint32_t currentFrameSlot_ = 0;

    struct TimelineSignal
    {
        VkSemaphore semaphore = VK_NULL_HANDLE;
        uint64_t value = 0;
    };

    std::vector<TimelineSignal> pendingFrameSignals_;
    std::vector<VkSemaphoreSubmitInfo> frameSignalScratch_; // reused per flush, avoids per-frame allocation

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
};
