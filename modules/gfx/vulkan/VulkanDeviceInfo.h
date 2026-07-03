#pragma once
#include <cstdint>

// Neutral hand-off PODs between the Vulkan renderer/backend and the FFmpeg-based
// Vulkan hwaccel bridge.
//
// WHY void*/uint64_t instead of Vulkan types: this header is shared by the renderer,
// the playback bridge, and graphics-core interfaces. Keeping it Vulkan/FFmpeg-free
// prevents public host interfaces from inheriting either dependency. Each side
// reinterpret_casts raw handle bits back to its own Vulkan typedefs. (x64 only:
// dispatchable + non-dispatchable handles are pointers.)

// Snapshot of the renderer's live Vulkan device, handed to the FFmpeg Vulkan hwaccel
// so it WRAPS this device (AVVulkanDeviceContext) instead of creating its own. Built
// by VulkanGraphicsBackend; consumed by CreateVulkanHwDevice() in the bridge.
struct VulkanDeviceInfo
{
    void* instance = nullptr;            // VkInstance
    void* physicalDevice = nullptr;      // VkPhysicalDevice
    void* device = nullptr;              // VkDevice
    void* getInstanceProcAddr = nullptr; // PFN_vkGetInstanceProcAddr
    const void* featuresChain = nullptr; // const VkPhysicalDeviceFeatures2* (lifetime: backend)

    const char* const* deviceExtensions = nullptr; // enabled device-extension names (lifetime: backend)
    int deviceExtensionCount = 0;

    int graphicsQueueFamily = -1;
    uint32_t graphicsQueueFlags = 0; // VkQueueFlagBits of the graphics family
    int videoDecodeQueueFamily = -1; // -1 when the device has no video-decode queue
    uint32_t videoDecodeQueueFlags = 0; // VkQueueFlagBits of the decode family
    uint32_t videoDecodeCaps = 0;    // VkVideoCodecOperationFlagBitsKHR bits of the decode family
    bool supportsVideoDecode = false;

    // VulkanQueueLock* (lifetime: backend). The bridge wires it into the hwdevice's
    // lock_queue/unlock_queue callbacks so FFmpeg's decode-thread queue submits are
    // serialized against the render thread's. void* to keep this header Vulkan/FFmpeg-free.
    void* queueLock = nullptr;

    // True when the device's queues were created with VK_KHR_internally_synchronized_queues
    // (VK_DEVICE_QUEUE_CREATE_INTERNALLY_SYNCHRONIZED_BIT_KHR). The driver then makes
    // concurrent submits safe, so the bridge does NOT wire FFmpeg's deprecated
    // lock_queue/unlock_queue callbacks (and the backend's VulkanQueueLock is a no-op).
    bool internalQueueSync = false;
};

// Snapshot of one decoded AVVkFrame's primary image + its timeline-semaphore sync
// state, read by the bridge (under the frame lock) and handed to the renderer so it
// can build a view, barrier, and register the wait/signal with the backend's submit.
struct VulkanFrameInfo
{
    // Identity of the decoder's hw-frames pool (the AVHWFramesContext address). The
    // renderer caches per-VkImage views/sets; when FFmpeg rebuilds its pool — possible
    // at an unchanged format, e.g. on seeks or stream switches — the old images die and
    // the driver may reuse their handle values, so the cache must be invalidated on any
    // change of this id, not only on format changes.
    uint64_t framesContextId = 0;
    uint64_t image = 0;     // VkImage  (AVVkFrame::img[0]; multiplanar single image)
    uint64_t semaphore = 0; // VkSemaphore (AVVkFrame::sem[0]; timeline)
    uint64_t semValue = 0;  // value to wait on before sampling (AVVkFrame::sem_value[0])
    int layout = 0;         // VkImageLayout the decode left the image in (AVVkFrame::layout[0])
    uint32_t queueFamily = 0; // current owning family (AVVkFrame::queue_family[0])
    int vkFormat = 0;       // VkFormat of the multiplanar image (for the YCbCr conversion)
    int colorSpace = 0;     // AVColorSpace (BT.601/709/...) -> VkSamplerYcbcrModelConversion
    int colorRange = 0;     // AVColorRange (MPEG/JPEG) -> VkSamplerYcbcrRange
    int width = 0;
    int height = 0;
    bool valid = false;
};
