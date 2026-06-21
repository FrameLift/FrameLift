#pragma once
#include <cstdint>

// Neutral hand-off PODs between the volk-based Vulkan renderer/backend and the
// FFmpeg-based Vulkan hwaccel bridge.
//
// WHY void*/uint64_t instead of Vulkan types: volk.h (VK_NO_PROTOTYPES, function-
// pointer dispatch) and FFmpeg's <libavutil/hwcontext_vulkan.h> (which pulls
// <vulkan/vulkan.h> WITH prototypes) are mutually exclusive within one translation
// unit. The backend/renderer see only volk; the bridge sees only the prototyped
// headers. These structs carry raw handle bits across that boundary so neither side
// includes the other's Vulkan headers. Each side reinterpret_casts back to its own
// Vulkan typedefs. (x64 only: dispatchable + non-dispatchable handles are pointers.)

// Snapshot of the renderer's live Vulkan device, handed to the FFmpeg Vulkan hwaccel
// so it WRAPS this device (AVVulkanDeviceContext) instead of creating its own. Built
// by VulkanGraphicsBackend; consumed by CreateVulkanHwDevice() in the bridge.
struct VulkanDeviceInfo
{
    void* instance = nullptr;            // VkInstance
    void* physicalDevice = nullptr;      // VkPhysicalDevice
    void* device = nullptr;              // VkDevice
    void* getInstanceProcAddr = nullptr; // PFN_vkGetInstanceProcAddr (volk's loaded global)
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
};

// Snapshot of one decoded AVVkFrame's primary image + its timeline-semaphore sync
// state, read by the bridge (under the frame lock) and handed to the renderer so it
// can build a view, barrier, and register the wait/signal with the backend's submit.
struct VulkanFrameInfo
{
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
