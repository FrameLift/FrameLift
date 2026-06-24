#include "VulkanGraphicsBackend.h"

#include "VulkanVideoRenderer.h"
#include "util.h"

// VMA lives in this TU. Use volk's dynamically loaded entry points (no static link),
// letting VMA pull the rest from the two getters we supply below.
#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <vk_mem_alloc.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_vulkan.h"

// Embedded SPIR-V for the fullscreen-triangle blit, reused for the layer composition
// pass (generated at build time; the video renderer uses the same shaders).
#include "video.frag.spv.h"
#include "video.vert.spv.h"

#include <framelift/Log.h>

#include <algorithm>
#include <cstring>
#include <optional>
#include <utility>

namespace
{
#ifndef NDEBUG
constexpr bool kEnableValidation = true;
#else
constexpr bool kEnableValidation = false;
#endif

// True if `name` appears in a vkEnumerate*ExtensionProperties result.
bool HasExt(const std::vector<VkExtensionProperties>& list, const char* name)
{
    for (const VkExtensionProperties& e : list)
    {
        if (std::strcmp(e.extensionName, name) == 0)
        {
            return true;
        }
    }
    return false;
}

VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                             VkDebugUtilsMessageTypeFlagsEXT /*types*/,
                                             const VkDebugUtilsMessengerCallbackDataEXT* data, void* /*ud*/)
{
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    {
        Log::Error("Vulkan: {}", data->pMessage);
    }
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    {
        Log::Warn("Vulkan: {}", data->pMessage);
    }
    return VK_FALSE;
}

// Records the staging->image copy for a freshly-created (UNDEFINED) image, leaving it
// in SHADER_READ_ONLY_OPTIMAL. Used by CreateUiTexture via ImmediateSubmit.
struct UiCopyJob
{
    VkBuffer staging;
    VkImage image;
    uint32_t w, h;
};

void RecordUiCopy(VkCommandBuffer cmd, void* ud)
{
    const UiCopyJob& j = *static_cast<UiCopyJob*>(ud);

    VkImageMemoryBarrier toDst{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.image = j.image;
    toDst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &toDst);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {j.w, j.h, 1};
    vkCmdCopyBufferToImage(cmd, j.staging, j.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VkImageMemoryBarrier toRead{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toRead.image = j.image;
    toRead.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &toRead);
}
} // namespace

bool VulkanGraphicsBackend::IsSupported()
{
    if (volkInitialize() != VK_SUCCESS)
    {
        return false; // no Vulkan loader / driver
    }
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_3; // FFmpeg's Vulkan hwaccel requires an instance >= 1.3
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &app;
    VkInstance inst = VK_NULL_HANDLE;
    if (vkCreateInstance(&ci, nullptr, &inst) != VK_SUCCESS)
    {
        return false;
    }
    volkLoadInstanceOnly(inst);
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(inst, &count, nullptr);
    vkDestroyInstance(inst, nullptr);
    return count > 0;
}

uint64_t VulkanGraphicsBackend::PreWindowCreate()
{
    // No GL attributes; just request a Vulkan-capable window.
    return SDL_WINDOW_VULKAN;
}

void VulkanGraphicsBackend::OnWindowCreated(SDL_Window* window)
{
    window_ = window;

    if (volkInitialize() != VK_SUCCESS)
    {
        Fatal("Vulkan loader not found (volkInitialize failed). Is a GPU driver installed?");
    }

    // ── Instance ───────────────────────────────────────────────────────────────
    uint32_t sdlExtCount = 0;
    const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&sdlExtCount);
    std::vector<const char*> instExts(sdlExts, sdlExts + sdlExtCount);

    // Enable the validation layer only when present (so a dev box without the SDK still runs).
    const char* const kValidationLayer = "VK_LAYER_KHRONOS_validation";
    bool useValidation = false;
    if (kEnableValidation)
    {
        uint32_t layerCount = 0;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        std::vector<VkLayerProperties> layers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, layers.data());
        for (const VkLayerProperties& l : layers)
        {
            if (std::strcmp(l.layerName, kValidationLayer) == 0)
            {
                useValidation = true;
                break;
            }
        }
        if (useValidation)
        {
            instExts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }
    }

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "FrameLift";
    // 1.3 instance: required by FFmpeg's Vulkan hwaccel (AVVulkanDeviceContext). The
    // device may still come back at 1.1 on older GPUs (video unsupported → CPU fallback).
    app.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = static_cast<uint32_t>(instExts.size());
    ici.ppEnabledExtensionNames = instExts.data();
    if (useValidation)
    {
        ici.enabledLayerCount = 1;
        ici.ppEnabledLayerNames = &kValidationLayer;
    }
    if (vkCreateInstance(&ici, nullptr, &instance_) != VK_SUCCESS)
    {
        Fatal("Vulkan instance creation failed");
    }
    volkLoadInstance(instance_);

    if (useValidation)
    {
        VkDebugUtilsMessengerCreateInfoEXT dbg{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        dbg.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        dbg.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        dbg.pfnUserCallback = DebugCallback;
        vkCreateDebugUtilsMessengerEXT(instance_, &dbg, nullptr, &debugMessenger_);
    }

    // ── Surface ────────────────────────────────────────────────────────────────
    if (!SDL_Vulkan_CreateSurface(window_, instance_, nullptr, &surface_))
    {
        Fatal(SDL_GetError());
    }

    // ── Physical device + device ───────────────────────────────────────────────
    // Build the enabled-feature chain once. We try a 1.3 device with the Vulkan-video
    // decode stack first (so FFmpeg can decode straight onto our device, #18); if that
    // device can't be created (older GPU / no video) we fall back to a minimal 1.1
    // device, exactly like Phase 2, and the player uses the CPU-RGBA8 path.
    enabledF13_ = VkPhysicalDeviceVulkan13Features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    enabledF13_.synchronization2 = VK_TRUE;
    enabledF12_ = VkPhysicalDeviceVulkan12Features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    enabledF12_.timelineSemaphore = VK_TRUE;
    enabledF11_ = VkPhysicalDeviceVulkan11Features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    enabledF11_.samplerYcbcrConversion = VK_TRUE;
    // FFmpeg defaults these on; mirror them so our recorded device_features matches.
    enabledFeatures2_ = VkPhysicalDeviceFeatures2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    enabledFeatures2_.features.shaderImageGatherExtended = VK_TRUE;
    enabledFeatures2_.features.fragmentStoresAndAtomics = VK_TRUE;
    enabledFeatures2_.features.shaderInt64 = VK_TRUE;
    enabledFeatures2_.features.vertexPipelineStoresAndAtomics = VK_TRUE;

    // Candidate device + chosen queue families and the extension set we will enable.
    struct DevicePick
    {
        VkPhysicalDevice phys = VK_NULL_HANDLE;
        uint32_t graphicsFamily = 0;
        uint32_t presentFamily = 0;
        std::vector<const char*> deviceExts;
        bool internalSync = false; // VK_KHR_internally_synchronized_queues usable
    };

    uint32_t physCount = 0;
    vkEnumeratePhysicalDevices(instance_, &physCount, nullptr);
    std::vector<VkPhysicalDevice> physDevices(physCount);
    vkEnumeratePhysicalDevices(instance_, &physCount, physDevices.data());

    // Pick the first device satisfying the tier (graphics + present + required
    // extensions/features), preferring a discrete GPU. The video tier additionally
    // requires the Vulkan-video decode stack and the 1.1/1.2/1.3 feature set we enable.
    auto tryPick = [&](bool withVideo) -> std::optional<DevicePick> {
        std::optional<DevicePick> best;
        bool bestDiscrete = false;
        for (VkPhysicalDevice pd : physDevices)
        {
            VkPhysicalDeviceProperties pp{};
            vkGetPhysicalDeviceProperties(pd, &pp);
            if (pp.apiVersion < (withVideo ? VK_API_VERSION_1_3 : VK_API_VERSION_1_1))
            {
                continue;
            }

            uint32_t qfCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(pd, &qfCount, nullptr);
            std::vector<VkQueueFamilyProperties> qfp(qfCount);
            vkGetPhysicalDeviceQueueFamilyProperties(pd, &qfCount, qfp.data());
            int gfx = -1, present = -1;
            for (uint32_t i = 0; i < qfCount; ++i)
            {
                if (gfx < 0 && (qfp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
                {
                    gfx = static_cast<int>(i);
                }
                VkBool32 sup = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, surface_, &sup);
                if (present < 0 && sup)
                {
                    present = static_cast<int>(i);
                }
            }
            if (gfx < 0 || present < 0)
            {
                continue;
            }
            // Prefer presenting on the graphics family when it supports it (the common case,
            // and it lets the swapchain use EXCLUSIVE sharing).
            VkBool32 gfxPresents = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(pd, static_cast<uint32_t>(gfx), surface_, &gfxPresents);
            if (gfxPresents)
            {
                present = gfx;
            }

            uint32_t extCount = 0;
            vkEnumerateDeviceExtensionProperties(pd, nullptr, &extCount, nullptr);
            std::vector<VkExtensionProperties> exts(extCount);
            vkEnumerateDeviceExtensionProperties(pd, nullptr, &extCount, exts.data());
            if (!HasExt(exts, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
            {
                continue;
            }
            if (withVideo && (!HasExt(exts, VK_KHR_VIDEO_QUEUE_EXTENSION_NAME) ||
                              !HasExt(exts, VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME)))
            {
                continue;
            }

            DevicePick cand;
            cand.phys = pd;
            cand.graphicsFamily = static_cast<uint32_t>(gfx);
            cand.presentFamily = static_cast<uint32_t>(present);
            cand.deviceExts.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

            if (withVideo)
            {
                // Require the features we enable below: a device missing any can't take the video tier.
                VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
                VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
                VkPhysicalDeviceVulkan11Features f11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
                VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
                f2.pNext = &f11;
                f11.pNext = &f12;
                f12.pNext = &f13;
                vkGetPhysicalDeviceFeatures2(pd, &f2);
                const bool featuresOk = f11.samplerYcbcrConversion && f12.timelineSemaphore &&
                                        f13.synchronization2 && f2.features.shaderImageGatherExtended &&
                                        f2.features.fragmentStoresAndAtomics && f2.features.shaderInt64 &&
                                        f2.features.vertexPipelineStoresAndAtomics;
                if (!featuresOk)
                {
                    continue;
                }

                cand.deviceExts.push_back(VK_KHR_VIDEO_QUEUE_EXTENSION_NAME);
                cand.deviceExts.push_back(VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME);
                // Codec + maintenance extensions vary by GPU; enable whichever are present.
                for (const char* opt : {VK_KHR_VIDEO_DECODE_H264_EXTENSION_NAME, VK_KHR_VIDEO_DECODE_H265_EXTENSION_NAME,
                                        VK_KHR_VIDEO_DECODE_AV1_EXTENSION_NAME, VK_KHR_VIDEO_MAINTENANCE_1_EXTENSION_NAME})
                {
                    if (HasExt(exts, opt))
                    {
                        cand.deviceExts.push_back(opt);
                    }
                }

                // Proper (non-deprecated) FFmpeg queue synchronization: when the driver
                // supports internally-synchronized queues, we create them with that bit and
                // skip FFmpeg's deprecated lock_queue callback + our host-side queue lock.
                if (HasExt(exts, VK_KHR_INTERNALLY_SYNCHRONIZED_QUEUES_EXTENSION_NAME))
                {
                    VkPhysicalDeviceInternallySynchronizedQueuesFeaturesKHR isq{
                        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INTERNALLY_SYNCHRONIZED_QUEUES_FEATURES_KHR};
                    VkPhysicalDeviceFeatures2 q2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
                    q2.pNext = &isq;
                    vkGetPhysicalDeviceFeatures2(pd, &q2);
                    if (isq.internallySynchronizedQueues)
                    {
                        cand.deviceExts.push_back(VK_KHR_INTERNALLY_SYNCHRONIZED_QUEUES_EXTENSION_NAME);
                        cand.internalSync = true;
                    }
                }
            }

            const bool discrete = pp.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
            if (!best || (discrete && !bestDiscrete))
            {
                best = std::move(cand);
                bestDiscrete = discrete;
            }
        }
        return best;
    };

    auto pick = tryPick(true);
    if (pick)
    {
        supportsVulkanVideo_ = true;
    }
    else
    {
        Log::Info("Vulkan: video-decode device unavailable; using a plain render device (CPU upload path)");
        pick = tryPick(false);
    }
    if (!pick)
    {
        Fatal("Vulkan device creation failed");
    }

    physicalDevice_ = pick->phys;
    graphicsQueueFamily_ = pick->graphicsFamily;
    presentQueueFamily_ = pick->presentFamily;
    internalQueueSync_ = pick->internalSync;
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physicalDevice_, &props);
    nvidiaAdapter_ = props.vendorID == 0x10DE;

    // ── Logical device ─────────────────────────────────────────────────────────
    // One queue per family (so the video-decode queue exists for FFmpeg to fetch), with a
    // SECOND queue on every graphics-capable family that allows it: ImGui's multi-viewport
    // rendering uses that second queue so secondary-window submits/presents do not share
    // queue index 0 with video work (#26).
    uint32_t allQfCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &allQfCount, nullptr);
    std::vector<VkQueueFamilyProperties> allQfp(allQfCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &allQfCount, allQfp.data());

    static const float kPrios[2] = {1.0f, 1.0f};
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    queueInfos.reserve(allQfCount);
    for (uint32_t i = 0; i < allQfCount; ++i)
    {
        const bool dualGraphics = (allQfp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && allQfp[i].queueCount >= 2;
        VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qci.queueFamilyIndex = i;
        qci.queueCount = dualGraphics ? 2 : 1;
        qci.pQueuePriorities = kPrios;
        if (internalQueueSync_)
        {
            // Driver-internal synchronization for every shared queue → no host-side lock.
            qci.flags |= VK_DEVICE_QUEUE_CREATE_INTERNALLY_SYNCHRONIZED_BIT_KHR;
        }
        queueInfos.push_back(qci);
    }

    // Feature chain handed to FFmpeg later via GetVulkanDeviceInfo (it walks
    // VkPhysicalDeviceFeatures2 -> 11/12/13). Link it once, before device creation.
    enabledFeatures2_.pNext = &enabledF11_;
    enabledF11_.pNext = &enabledF12_;
    enabledF12_.pNext = &enabledF13_;
    enabledF13_.pNext = nullptr;

    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
    dci.pQueueCreateInfos = queueInfos.data();
    dci.enabledExtensionCount = static_cast<uint32_t>(pick->deviceExts.size());
    dci.ppEnabledExtensionNames = pick->deviceExts.data();
    // The plain 1.1 fallback enables no extra features (matches the old minimal device);
    // only the video tier needs the 1.1/1.2/1.3 chain FFmpeg relies on.
    VkPhysicalDeviceInternallySynchronizedQueuesFeaturesKHR isqEnable{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INTERNALLY_SYNCHRONIZED_QUEUES_FEATURES_KHR};
    isqEnable.internallySynchronizedQueues = VK_TRUE;
    if (supportsVulkanVideo_)
    {
        dci.pNext = &enabledFeatures2_;
        if (internalQueueSync_)
        {
            // Chain the feature only for this synchronous create call; unlink it right
            // after so the member chain FFmpeg later walks doesn't dangle into this local.
            enabledF13_.pNext = &isqEnable;
        }
    }
    const VkResult devRes = vkCreateDevice(physicalDevice_, &dci, nullptr, &device_);
    enabledF13_.pNext = nullptr;
    if (devRes != VK_SUCCESS)
    {
        Fatal("Vulkan device creation failed");
    }
    volkLoadDevice(device_);

    vkGetDeviceQueue(device_, graphicsQueueFamily_, 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, presentQueueFamily_, 0, &presentQueue_);

    // When the device's queues are internally synchronized, the driver makes concurrent
    // submits safe — disable the host-side queue lock entirely (the FFmpeg bridge likewise
    // skips its deprecated lock_queue callback, via VulkanDeviceInfo::internalQueueSync).
    queueLock_.SetEnabled(!internalQueueSync_);
    Log::Info("Vulkan: queue synchronization = {}", internalQueueSync_ ? "internal (driver)" : "host lock");

    // Grab the second graphics-family queue for ImGui multi-viewport (#26). If the family
    // exposes only one queue, ImGui shares the render queue and the queue lock serializes
    // secondary-window submits/presents with video work.
    imguiQueue_ = graphicsQueue_;
    imguiQueueIndex_ = 0;
    if (graphicsQueueFamily_ < allQfCount && allQfp[graphicsQueueFamily_].queueCount >= 2)
    {
        vkGetDeviceQueue(device_, graphicsQueueFamily_, 1, &imguiQueue_);
        imguiQueueIndex_ = 1;
        Log::Info("Vulkan: dedicated ImGui multi-viewport queue (graphics family {}, index {})", graphicsQueueFamily_,
                  imguiQueueIndex_);
    }
    else
    {
        Log::Info("Vulkan: graphics family exposes a single queue; ImGui shares the render "
                  "queue (multi-viewport may contend with video)");
    }

    // Record the queue flags + (if video) detect the decode queue family and its codec
    // caps, so GetVulkanDeviceInfo can hand FFmpeg a complete qf[] list.
    if (supportsVulkanVideo_)
    {
        DetectVideoDecodeQueue();
    }
    if (!supportsVulkanVideo_ && graphicsQueueFamily_ < allQfCount)
    {
        // tryPick(true) may succeed on a device that lacks an actual decode queue;
        // DetectVideoDecodeQueue() clears the flag in that case. Still record graphics
        // flags for completeness.
        graphicsQueueFlags_ = allQfp[graphicsQueueFamily_].queueFlags;
    }

    // Record the enabled device-extension list for FFmpeg's wrap.
    enabledDeviceExtNames_.assign(pick->deviceExts.begin(), pick->deviceExts.end());
    enabledDeviceExtPtrs_.clear();
    enabledDeviceExtPtrs_.reserve(enabledDeviceExtNames_.size());
    for (const std::string& e : enabledDeviceExtNames_)
    {
        enabledDeviceExtPtrs_.push_back(e.c_str());
    }

    // ── VMA allocator (driven by volk's loaded pointers) ───────────────────────
    // Pinned to 1.1 (our allocations use no newer features); valid on the 1.1 fallback
    // device and the 1.3 video device alike. FFmpeg allocates its own frames, not via VMA.
    VmaVulkanFunctions vmaFns{};
    vmaFns.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vmaFns.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    VmaAllocatorCreateInfo allocInfo{};
    allocInfo.instance = instance_;
    allocInfo.physicalDevice = physicalDevice_;
    allocInfo.device = device_;
    allocInfo.vulkanApiVersion = VK_API_VERSION_1_1;
    allocInfo.pVulkanFunctions = &vmaFns;
    if (vmaCreateAllocator(&allocInfo, &allocator_) != VK_SUCCESS)
    {
        Fatal("VMA allocator creation failed");
    }

    // ── Command pool (graphics, resettable) ────────────────────────────────────
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = graphicsQueueFamily_;
    if (vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_) != VK_SUCCESS)
    {
        Fatal("Vulkan command pool creation failed");
    }

    VkCommandBufferAllocateInfo cbInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbInfo.commandPool = commandPool_;
    cbInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbInfo.commandBufferCount = kMaxFramesInFlight;
    if (vkAllocateCommandBuffers(device_, &cbInfo, commandBuffers_.data()) != VK_SUCCESS)
    {
        Fatal("Vulkan command buffer allocation failed");
    }

    if (!CreateSwapchain() || !CreateRenderPass() || !CreateLayerRenderPass() || !CreateCompositeResources() ||
        !CreateFramebuffers() || !CreateLayerTargets() || !CreateSyncObjects())
    {
        Fatal("Vulkan swapchain/render-pass setup failed");
    }

    // ── ImGui descriptor pool (generous, used by the ImGui Vulkan backend) ──────
    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
    };
    VkDescriptorPoolCreateInfo dpInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    dpInfo.maxSets = 1000;
    dpInfo.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes));
    dpInfo.pPoolSizes = poolSizes;
    if (vkCreateDescriptorPool(device_, &dpInfo, nullptr, &imguiDescriptorPool_) != VK_SUCCESS)
    {
        Fatal("Vulkan ImGui descriptor pool creation failed");
    }
}

bool VulkanGraphicsBackend::CreateSwapchain()
{
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &caps);

    // Surface format: prefer B8G8R8A8_UNORM / sRGB-nonlinear, else the first reported.
    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &fmtCount, formats.data());
    VkSurfaceFormatKHR chosen{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    if (!formats.empty())
    {
        chosen = formats[0];
        for (const VkSurfaceFormatKHR& f : formats)
        {
            if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                chosen = f;
                break;
            }
        }
    }

    // Present mode: always FIFO (the only universally-available mode, paced to the display
    // refresh). A video player gains nothing from MAILBOX/IMMEDIATE — those just render as fast
    // as the GPU allows, burning power for frames the display never shows — so we never use them.
    const VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;

    // Extent: honour the surface's currentExtent when fixed, else clamp the pixel size.
    VkExtent2D extent = caps.currentExtent;
    if (caps.currentExtent.width == UINT32_MAX)
    {
        int pw = 0, ph = 0;
        SDL_GetWindowSizeInPixels(window_, &pw, &ph);
        extent.width = std::clamp(static_cast<uint32_t>(pw), caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = std::clamp(static_cast<uint32_t>(ph), caps.minImageExtent.height, caps.maxImageExtent.height);
    }

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
    {
        imageCount = caps.maxImageCount;
    }

    VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    sci.surface = surface_;
    sci.minImageCount = imageCount;
    sci.imageFormat = chosen.format;
    sci.imageColorSpace = chosen.colorSpace;
    sci.imageExtent = extent;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    const uint32_t qfi[2] = {graphicsQueueFamily_, presentQueueFamily_};
    if (graphicsQueueFamily_ != presentQueueFamily_)
    {
        sci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        sci.queueFamilyIndexCount = 2;
        sci.pQueueFamilyIndices = qfi;
    }
    else
    {
        sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = presentMode;
    sci.clipped = VK_TRUE;
    // The previous swapchain (if any) is retired here and destroyed by RecreateSwapchain.
    sci.oldSwapchain = swapchain_;

    VkSwapchainKHR newSwapchain = VK_NULL_HANDLE;
    if (vkCreateSwapchainKHR(device_, &sci, nullptr, &newSwapchain) != VK_SUCCESS)
    {
        Log::Error("Vulkan swapchain creation failed");
        return false;
    }
    swapchain_ = newSwapchain;
    swapchainFormat_ = chosen.format;
    swapchainExtent_ = extent;

    uint32_t scImgCount = 0;
    vkGetSwapchainImagesKHR(device_, swapchain_, &scImgCount, nullptr);
    swapchainImages_.resize(scImgCount);
    vkGetSwapchainImagesKHR(device_, swapchain_, &scImgCount, swapchainImages_.data());

    swapchainImageViews_.resize(scImgCount);
    for (uint32_t i = 0; i < scImgCount; ++i)
    {
        VkImageViewCreateInfo iv{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        iv.image = swapchainImages_[i];
        iv.viewType = VK_IMAGE_VIEW_TYPE_2D;
        iv.format = swapchainFormat_;
        iv.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(device_, &iv, nullptr, &swapchainImageViews_[i]) != VK_SUCCESS)
        {
            Log::Error("Vulkan swapchain image view creation failed");
            return false;
        }
    }
    // ImGui's MinImageCount must be >= 2 and <= ImageCount; the actual count satisfies both.
    minImageCount_ = scImgCount;

    Log::Debug("Vulkan swapchain created: imageCount={} extent={}x{}",
              scImgCount, extent.width, extent.height);
    return true;
}

bool VulkanGraphicsBackend::CreateRenderPass()
{
    VkAttachmentDescription color{};
    color.format = swapchainFormat_;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    // No clear: the opaque video blit in RecordComposite covers every pixel before the UI
    // draws over it (the video layer is swapchain-sized and letterboxed over black), so
    // the previous contents need not be loaded or cleared.
    color.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rp.attachmentCount = 1;
    rp.pAttachments = &color;
    rp.subpassCount = 1;
    rp.pSubpasses = &subpass;
    rp.dependencyCount = 1;
    rp.pDependencies = &dep;
    return vkCreateRenderPass(device_, &rp, nullptr, &renderPass_) == VK_SUCCESS;
}

bool VulkanGraphicsBackend::CreateFramebuffers()
{
    framebuffers_.resize(swapchainImageViews_.size());
    for (size_t i = 0; i < swapchainImageViews_.size(); ++i)
    {
        VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fb.renderPass = renderPass_;
        fb.attachmentCount = 1;
        fb.pAttachments = &swapchainImageViews_[i];
        fb.width = swapchainExtent_.width;
        fb.height = swapchainExtent_.height;
        fb.layers = 1;
        if (vkCreateFramebuffer(device_, &fb, nullptr, &framebuffers_[i]) != VK_SUCCESS)
        {
            return false;
        }
    }
    return true;
}

// Render pass for the offscreen video / UI layers: clear, render, then leave the image
// in SHADER_READ_ONLY_OPTIMAL ready for the composition pass to sample. Format-only, so
// it survives swapchain recreation; shared by both layer framebuffers.
bool VulkanGraphicsBackend::CreateLayerRenderPass()
{
    VkAttachmentDescription color{};
    color.format = swapchainFormat_;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    // Incoming: a re-render of a layer must wait for the previous frame's composite that
    // sampled it (WAR) and any prior write (WAW). Outgoing: this frame's layer write must
    // be visible to the composite pass's fragment-shader sampling.
    VkSubpassDependency deps[2]{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rp.attachmentCount = 1;
    rp.pAttachments = &color;
    rp.subpassCount = 1;
    rp.pSubpasses = &subpass;
    rp.dependencyCount = 2;
    rp.pDependencies = deps;
    return vkCreateRenderPass(device_, &rp, nullptr, &layerRenderPass_) == VK_SUCCESS;
}

// Fullscreen-triangle pipeline that samples the video layer into the swapchain render
// pass as an opaque copy (the layer is already letterboxed over black). The UI is no
// longer a composited layer — ImGui draws straight into this pass in RecordComposite.
bool VulkanGraphicsBackend::CreateCompositePipeline(VkPipeline& out)
{
    VkShaderModuleCreateInfo vci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    vci.codeSize = sizeof(kVideoVertSpv);
    vci.pCode = kVideoVertSpv;
    VkShaderModule vs = VK_NULL_HANDLE;
    vkCreateShaderModule(device_, &vci, nullptr, &vs);
    VkShaderModuleCreateInfo fci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    fci.codeSize = sizeof(kVideoFragSpv);
    fci.pCode = kVideoFragSpv;
    VkShaderModule fs = VK_NULL_HANDLE;
    vkCreateShaderModule(device_, &fci, nullptr, &fs);
    if (vs == VK_NULL_HANDLE || fs == VK_NULL_HANDLE)
    {
        Log::Error("VulkanGraphicsBackend: composite shader module creation failed");
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // The video layer is opaque, so the pipeline disables blending and copies straight.
    VkPipelineColorBlendAttachmentState cba{};
    cba.blendEnable = VK_FALSE;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.colorBlendOp = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.alphaBlendOp = VK_BLEND_OP_ADD;
    cba.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;

    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = dyn;

    VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gp.stageCount = 2;
    gp.pStages = stages;
    gp.pVertexInputState = &vi;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms;
    gp.pColorBlendState = &cb;
    gp.pDynamicState = &ds;
    gp.layout = compositePipelineLayout_;
    gp.renderPass = renderPass_; // the swapchain pass (composition target)
    gp.subpass = 0;

    const VkResult r = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &out);
    vkDestroyShaderModule(device_, vs, nullptr);
    vkDestroyShaderModule(device_, fs, nullptr);
    if (r != VK_SUCCESS)
    {
        Log::Error("VulkanGraphicsBackend: composite pipeline creation failed");
        return false;
    }
    return true;
}

// Size-independent composition resources (sampler, set layout, pool, the two pipelines
// and their descriptor sets). Created once; the sets are re-pointed at the layer views
// each time CreateLayerTargets() runs.
bool VulkanGraphicsBackend::CreateCompositeResources()
{
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = VK_FILTER_LINEAR;
    si.minFilter = VK_FILTER_LINEAR;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    if (vkCreateSampler(device_, &si, nullptr, &compositeSampler_) != VK_SUCCESS)
    {
        return false;
    }

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount = 1;
    li.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(device_, &li, nullptr, &compositeSetLayout_) != VK_SUCCESS)
    {
        return false;
    }

    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pi.maxSets = 1;
    pi.poolSizeCount = 1;
    pi.pPoolSizes = &ps;
    if (vkCreateDescriptorPool(device_, &pi, nullptr, &compositeDescPool_) != VK_SUCCESS)
    {
        return false;
    }
    VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dai.descriptorPool = compositeDescPool_;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts = &compositeSetLayout_;
    if (vkAllocateDescriptorSets(device_, &dai, &videoSet_) != VK_SUCCESS)
    {
        return false;
    }

    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &compositeSetLayout_;
    if (vkCreatePipelineLayout(device_, &pl, nullptr, &compositePipelineLayout_) != VK_SUCCESS)
    {
        return false;
    }

    return CreateCompositePipeline(compositeVideoPipeline_);
}

// The offscreen video-layer target (swapchain-sized). Recreated with the swapchain; the
// composite descriptor set is re-pointed at the fresh view here.
bool VulkanGraphicsBackend::CreateLayerTargets()
{
    auto makeLayer = [&](LayerTarget& t) -> bool
    {
        VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.format = swapchainFormat_;
        ici.extent = {swapchainExtent_.width, swapchainExtent_.height, 1};
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VmaAllocationCreateInfo aci{};
        aci.usage = VMA_MEMORY_USAGE_AUTO;
        if (vmaCreateImage(allocator_, &ici, &aci, &t.image, &t.alloc, nullptr) != VK_SUCCESS)
        {
            return false;
        }
        VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vci.image = t.image;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = swapchainFormat_;
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(device_, &vci, nullptr, &t.view) != VK_SUCCESS)
        {
            return false;
        }
        VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fb.renderPass = layerRenderPass_;
        fb.attachmentCount = 1;
        fb.pAttachments = &t.view;
        fb.width = swapchainExtent_.width;
        fb.height = swapchainExtent_.height;
        fb.layers = 1;
        return vkCreateFramebuffer(device_, &fb, nullptr, &t.framebuffer) == VK_SUCCESS;
    };
    if (!makeLayer(videoLayer_))
    {
        Log::Error("VulkanGraphicsBackend: layer target creation failed");
        return false;
    }

    VkDescriptorImageInfo vInfo{compositeSampler_, videoLayer_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = videoSet_;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &vInfo;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);

    // A fresh image holds no content — force the video layer to render before the first composite.
    videoLayerValid_ = false;
    return true;
}

void VulkanGraphicsBackend::DestroyLayerTargets()
{
    LayerTarget* t = &videoLayer_;
    if (t->framebuffer != VK_NULL_HANDLE)
    {
        vkDestroyFramebuffer(device_, t->framebuffer, nullptr);
    }
    if (t->view != VK_NULL_HANDLE)
    {
        vkDestroyImageView(device_, t->view, nullptr);
    }
    if (t->image != VK_NULL_HANDLE)
    {
        vmaDestroyImage(allocator_, t->image, t->alloc);
    }
    *t = {};
}

// Composite onto the acquired swapchain image (called from SwapBuffers): blit the video
// layer as an opaque copy, then record the ImGui draw data straight over it in the same
// pass. Drawing ImGui here — instead of into a separate full-screen UI layer that is then
// alpha-composited — keeps the UI cost to the pixels ImGui actually touches, matching the
// OpenGL backend. The video blit is opaque and covers every pixel, so the pass needs no
// colour clear (renderPass_ uses LOAD_OP_DONT_CARE).
void VulkanGraphicsBackend::RecordComposite()
{
    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass = renderPass_;
    rp.framebuffer = framebuffers_[imageIndex_];
    rp.renderArea.extent = swapchainExtent_;
    rp.clearValueCount = 0;
    rp.pClearValues = nullptr;
    vkCmdBeginRenderPass(currentCmd_, &rp, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{};
    vp.x = 0.0f;
    vp.y = 0.0f;
    vp.width = static_cast<float>(swapchainExtent_.width);
    vp.height = static_cast<float>(swapchainExtent_.height);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    VkRect2D scissor{{0, 0}, swapchainExtent_};
    vkCmdSetViewport(currentCmd_, 0, 1, &vp);
    vkCmdSetScissor(currentCmd_, 0, 1, &scissor);

    vkCmdBindPipeline(currentCmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, compositeVideoPipeline_);
    vkCmdBindDescriptorSets(currentCmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, compositePipelineLayout_, 0, 1, &videoSet_, 0,
                            nullptr);
    vkCmdDraw(currentCmd_, 3, 1, 0, 0);

    // ImGui draws over the opaque video with its own per-vertex alpha blending; its
    // pipeline was built against renderPass_ (this swapchain pass) at init. GetDrawData()
    // stays valid from the host's ImGui::Render() until the next NewFrame.
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), currentCmd_);

    vkCmdEndRenderPass(currentCmd_);
}

bool VulkanGraphicsBackend::CreateSyncObjects()
{
    VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i)
    {
        if (vkCreateSemaphore(device_, &sem, nullptr, &imageAvailable_[i]) != VK_SUCCESS ||
            vkCreateFence(device_, &fence, nullptr, &inFlightFences_[i]) != VK_SUCCESS)
        {
            return false;
        }
    }
    // One render-finished semaphore per swapchain image: it is signalled by that
    // image's submit and waited on by its present, so it must not be shared.
    renderFinished_.resize(swapchainImages_.size());
    for (auto& s : renderFinished_)
    {
        if (vkCreateSemaphore(device_, &sem, nullptr, &s) != VK_SUCCESS)
        {
            return false;
        }
    }
    imagesInFlight_.assign(swapchainImages_.size(), VK_NULL_HANDLE);
    return true;
}

void VulkanGraphicsBackend::DestroySwapchain()
{
    // The offscreen layers are swapchain-sized, so they recreate with it.
    DestroyLayerTargets();
    for (VkFramebuffer fb : framebuffers_)
    {
        vkDestroyFramebuffer(device_, fb, nullptr);
    }
    framebuffers_.clear();
    for (VkImageView v : swapchainImageViews_)
    {
        vkDestroyImageView(device_, v, nullptr);
    }
    swapchainImageViews_.clear();
    swapchainImages_.clear();
}

bool VulkanGraphicsBackend::RecreateSwapchain()
{
    // Skip while minimized (zero-size) — try again next frame.
    int pw = 0, ph = 0;
    SDL_GetWindowSizeInPixels(window_, &pw, &ph);
    if (pw == 0 || ph == 0)
    {
        return false;
    }

    vkDeviceWaitIdle(device_);

    VkSwapchainKHR old = swapchain_;
    DestroySwapchain();
    for (VkSemaphore s : renderFinished_)
    {
        vkDestroySemaphore(device_, s, nullptr);
    }
    renderFinished_.clear();

    const bool ok = CreateSwapchain() && CreateFramebuffers() && CreateLayerTargets();
    if (old != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(device_, old, nullptr);
    }
    // Re-create the per-image render-finished semaphores + in-flight tracking.
    VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    renderFinished_.resize(swapchainImages_.size());
    for (auto& s : renderFinished_)
    {
        vkCreateSemaphore(device_, &sem, nullptr, &s);
    }
    imagesInFlight_.assign(swapchainImages_.size(), VK_NULL_HANDLE);
    return ok;
}

std::unique_ptr<IVideoRenderer> VulkanGraphicsBackend::CreateVideoRenderer()
{
    return std::make_unique<VulkanVideoRenderer>(this);
}

uintptr_t VulkanGraphicsBackend::CreateUiTexture(const unsigned char* rgba, int w, int h)
{
    if (!rgba || w <= 0 || h <= 0)
    {
        return 0;
    }
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(w) * h * 4;

    // Sampled RGBA image.
    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_R8G8B8A8_UNORM;
    ici.extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo iaci{};
    iaci.usage = VMA_MEMORY_USAGE_AUTO;
    UiTexture t{};
    if (vmaCreateImage(allocator_, &ici, &iaci, &t.image, &t.alloc, nullptr) != VK_SUCCESS)
    {
        return 0;
    }

    // Host-visible staging buffer.
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = bytes;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VmaAllocationCreateInfo baci{};
    baci.usage = VMA_MEMORY_USAGE_AUTO;
    baci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VkBuffer staging = VK_NULL_HANDLE;
    VmaAllocation stagingAlloc = nullptr;
    VmaAllocationInfo sInfo{};
    if (vmaCreateBuffer(allocator_, &bci, &baci, &staging, &stagingAlloc, &sInfo) != VK_SUCCESS)
    {
        vmaDestroyImage(allocator_, t.image, t.alloc);
        return 0;
    }
    std::memcpy(sInfo.pMappedData, rgba, static_cast<size_t>(bytes));

    UiCopyJob job{staging, t.image, static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
    ImmediateSubmit(RecordUiCopy, &job);
    vmaDestroyBuffer(allocator_, staging, stagingAlloc);

    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = t.image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = VK_FORMAT_R8G8B8A8_UNORM;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(device_, &vci, nullptr, &t.view) != VK_SUCCESS)
    {
        vmaDestroyImage(allocator_, t.image, t.alloc);
        return 0;
    }

    // Register with the ImGui Vulkan backend (uses its internal sampler).
    t.set = ImGui_ImplVulkan_AddTexture(t.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    uiTextures_.push_back(t);
    return reinterpret_cast<uintptr_t>(t.set);
}

void* VulkanGraphicsBackend::GetProcAddr(const char* /*name*/) const
{
    return nullptr; // Vulkan entry points are resolved by volk, not a GL-style loader.
}

bool VulkanGraphicsBackend::BeginFrame()
{
    vkWaitForFences(device_, 1, &inFlightFences_[currentFrame_], VK_TRUE, UINT64_MAX);

    VkResult acq = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, imageAvailable_[currentFrame_],
                                         VK_NULL_HANDLE, &imageIndex_);
    if (acq == VK_ERROR_OUT_OF_DATE_KHR)
    {
        RecreateSwapchain();
        return false;
    }
    if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR)
    {
        return false;
    }

    // If a prior frame still uses this image, wait on its fence before reusing it.
    if (imagesInFlight_[imageIndex_] != VK_NULL_HANDLE)
    {
        vkWaitForFences(device_, 1, &imagesInFlight_[imageIndex_], VK_TRUE, UINT64_MAX);
    }
    imagesInFlight_[imageIndex_] = inFlightFences_[currentFrame_];
    vkResetFences(device_, 1, &inFlightFences_[currentFrame_]);

    // Reset the per-frame timeline waits/signals; the video renderer re-registers the
    // zero-copy frame it samples this frame (if any) during Draw().
    frameWaitSems_.clear();
    frameWaitValues_.clear();
    frameWaitStages_.clear();
    frameSignalSems_.clear();
    frameSignalValues_.clear();
    framePreCmds_.clear();

    currentCmd_ = commandBuffers_[currentFrame_];
    vkResetCommandBuffer(currentCmd_, 0);

    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(currentCmd_, &begin);

    // Resolve whether to (re-)render the video layer this frame. A layer with no content
    // yet (first frame / after a resize) must render regardless of the dirty hint.
    frameRenderVideo_ = videoDirty_ || !videoLayerValid_;

    // Open the video-layer pass now so player_->RenderFrame() records into it. When the
    // video is reused (cached), we skip the pass and the video renderer — seeing
    // IsRecordingVideoLayer()==false — records nothing; the composite reuses videoLayer_.
    if (frameRenderVideo_)
    {
        VkClearValue clear{};
        clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}}; // opaque black (letterbox bars)
        VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rp.renderPass = layerRenderPass_;
        rp.framebuffer = videoLayer_.framebuffer;
        rp.renderArea.extent = swapchainExtent_;
        rp.clearValueCount = 1;
        rp.pClearValues = &clear;
        vkCmdBeginRenderPass(currentCmd_, &rp, VK_SUBPASS_CONTENTS_INLINE);
        recordingVideoLayer_ = true;
    }

    frameActive_ = true;
    return true;
}

void VulkanGraphicsBackend::SetFrameDirty(bool videoDirty, bool /*uiDirty*/)
{
    // uiDirty is ignored: the UI is recorded into the swapchain composite pass every frame
    // (RecordComposite) rather than cached in an offscreen layer. Only the video layer is
    // cached, so only videoDirty gates work here.
    videoDirty_ = videoDirty;
}

void VulkanGraphicsBackend::SwapBuffers()
{
    if (!frameActive_)
    {
        return;
    }
    frameActive_ = false;

    // Both layer passes are already closed (video in ImGuiNewFrame, UI in
    // ImGuiRenderDrawData); composite them onto the acquired swapchain image.
    RecordComposite();
    vkEndCommandBuffer(currentCmd_);

    // Combine the binary swapchain-acquire wait + per-image render-finished signal with
    // any zero-copy video frame's timeline wait/signal registered by the renderer this
    // frame. Binary semaphores get a dummy 0 in the parallel value arrays.
    std::vector<VkSemaphore> waitSems{imageAvailable_[currentFrame_]};
    std::vector<VkPipelineStageFlags> waitStages{VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    std::vector<uint64_t> waitValues{0};
    for (size_t i = 0; i < frameWaitSems_.size(); ++i)
    {
        waitSems.push_back(frameWaitSems_[i]);
        waitStages.push_back(frameWaitStages_[i]);
        waitValues.push_back(frameWaitValues_[i]);
    }

    std::vector<VkSemaphore> signalSems{renderFinished_[imageIndex_]};
    std::vector<uint64_t> signalValues{0};
    for (size_t i = 0; i < frameSignalSems_.size(); ++i)
    {
        signalSems.push_back(frameSignalSems_[i]);
        signalValues.push_back(frameSignalValues_[i]);
    }

    const bool hasTimeline = !frameWaitSems_.empty() || !frameSignalSems_.empty();
    VkTimelineSemaphoreSubmitInfo timeline{VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
    timeline.waitSemaphoreValueCount = static_cast<uint32_t>(waitValues.size());
    timeline.pWaitSemaphoreValues = waitValues.data();
    timeline.signalSemaphoreValueCount = static_cast<uint32_t>(signalValues.size());
    timeline.pSignalSemaphoreValues = signalValues.data();

    // Any zero-copy frame transition CBs run first (in the same submit), then the main
    // render CB. One submit per frame keeps the queue from stalling ahead of ImGui's
    // multi-viewport submits (#26).
    std::vector<VkCommandBuffer> cmds = framePreCmds_;
    cmds.push_back(currentCmd_);

    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.pNext = hasTimeline ? &timeline : nullptr;
    submit.waitSemaphoreCount = static_cast<uint32_t>(waitSems.size());
    submit.pWaitSemaphores = waitSems.data();
    submit.pWaitDstStageMask = waitStages.data();
    submit.commandBufferCount = static_cast<uint32_t>(cmds.size());
    submit.pCommandBuffers = cmds.data();
    submit.signalSemaphoreCount = static_cast<uint32_t>(signalSems.size());
    submit.pSignalSemaphores = signalSems.data();

    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &renderFinished_[imageIndex_];
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain_;
    present.pImageIndices = &imageIndex_;

    // Submit + present under the queue locks: FFmpeg's decode thread and ImGui's
    // platform-window backend can submit/present on queues from this device, and a
    // VkQueue is not thread-safe.
    {
        VulkanQueueGuard guard(queueLock_, graphicsQueueFamily_, 0);
        vkQueueSubmit(graphicsQueue_, 1, &submit, inFlightFences_[currentFrame_]);
    }

    VkResult pres;
    {
        VulkanQueueGuard guard(queueLock_, presentQueueFamily_, 0);
        pres = vkQueuePresentKHR(presentQueue_, &present);
    }
    if (pres == VK_ERROR_OUT_OF_DATE_KHR || pres == VK_SUBOPTIMAL_KHR)
    {
        RecreateSwapchain();
    }

    currentFrame_ = (currentFrame_ + 1) % kMaxFramesInFlight;

    if (!shown_)
    {
        shown_ = true;
        SDL_ShowWindow(window_);
    }
}

void VulkanGraphicsBackend::SetVSync(bool /*enabled*/)
{
    // No-op: the Vulkan swapchain is always FIFO (vsynced) — see CreateSwapchain. The vsync
    // toggle only has meaning on the OpenGL backend. Kept to satisfy the IGraphicsBackend ABI.
}

bool VulkanGraphicsBackend::ImmediateSubmit(void (*record)(VkCommandBuffer, void*), void* ud)
{
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = commandPool_;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device_, &ai, &cmd) != VK_SUCCESS)
    {
        return false;
    }

    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);
    record(cmd, ud);
    vkEndCommandBuffer(cmd);

    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence = VK_NULL_HANDLE;
    vkCreateFence(device_, &fi, nullptr, &fence);

    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    {
        // Only the submit needs the queue lock; the fence wait is device-level and must
        // not hold the lock (it would block FFmpeg's decode thread).
        VulkanQueueGuard guard(queueLock_, graphicsQueueFamily_, 0);
        vkQueueSubmit(graphicsQueue_, 1, &submit, fence);
    }
    vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX);

    vkDestroyFence(device_, fence, nullptr);
    vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);
    return true;
}

void VulkanGraphicsBackend::DetectVideoDecodeQueue()
{
    uint32_t qfCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties2(physicalDevice_, &qfCount, nullptr);
    if (qfCount == 0)
    {
        supportsVulkanVideo_ = false;
        return;
    }

    std::vector<VkQueueFamilyProperties2> props(qfCount, {VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2});
    std::vector<VkQueueFamilyVideoPropertiesKHR> vprops(qfCount,
                                                        {VK_STRUCTURE_TYPE_QUEUE_FAMILY_VIDEO_PROPERTIES_KHR});
    for (uint32_t i = 0; i < qfCount; ++i)
    {
        props[i].pNext = &vprops[i];
    }
    vkGetPhysicalDeviceQueueFamilyProperties2(physicalDevice_, &qfCount, props.data());

    if (graphicsQueueFamily_ < qfCount)
    {
        graphicsQueueFlags_ = props[graphicsQueueFamily_].queueFamilyProperties.queueFlags;
    }

    videoDecodeQueueFamily_ = -1;
    for (uint32_t i = 0; i < qfCount; ++i)
    {
        if (props[i].queueFamilyProperties.queueFlags & VK_QUEUE_VIDEO_DECODE_BIT_KHR)
        {
            videoDecodeQueueFamily_ = static_cast<int>(i);
            videoDecodeQueueFlags_ = props[i].queueFamilyProperties.queueFlags;
            videoDecodeCaps_ = vprops[i].videoCodecOperations;
            break;
        }
    }

    if (videoDecodeQueueFamily_ < 0)
    {
        // Extensions enabled but the implementation exposes no decode queue → no zero-copy.
        supportsVulkanVideo_ = false;
        return;
    }

    // Default queue setup creates one queue per family, so the decode
    // queue exists; fetch it (used for diagnostics — FFmpeg fetches its own by family).
    vkGetDeviceQueue(device_, static_cast<uint32_t>(videoDecodeQueueFamily_), 0, &videoDecodeQueue_);
    Log::Info("Vulkan: video-decode queue family {} (codec ops 0x{:x})", videoDecodeQueueFamily_,
              static_cast<uint32_t>(videoDecodeCaps_));
}

bool VulkanGraphicsBackend::GetVulkanDeviceInfo(VulkanDeviceInfo& out) const noexcept
{
    if (device_ == VK_NULL_HANDLE)
    {
        return false;
    }
    out.instance = instance_;
    out.physicalDevice = physicalDevice_;
    out.device = device_;
    out.getInstanceProcAddr = reinterpret_cast<void*>(vkGetInstanceProcAddr);
    out.featuresChain = &enabledFeatures2_;
    out.deviceExtensions = enabledDeviceExtPtrs_.empty() ? nullptr : enabledDeviceExtPtrs_.data();
    out.deviceExtensionCount = static_cast<int>(enabledDeviceExtPtrs_.size());
    out.graphicsQueueFamily = static_cast<int>(graphicsQueueFamily_);
    out.graphicsQueueFlags = graphicsQueueFlags_;
    out.videoDecodeQueueFamily = videoDecodeQueueFamily_;
    out.videoDecodeQueueFlags = videoDecodeQueueFlags_;
    out.videoDecodeCaps = videoDecodeCaps_;
    out.supportsVideoDecode = supportsVulkanVideo_;
    out.queueLock = &queueLock_;
    out.internalQueueSync = internalQueueSync_;
    return true;
}

void VulkanGraphicsBackend::AddFrameWait(VkSemaphore sem, uint64_t waitValue, VkPipelineStageFlags stage)
{
    frameWaitSems_.push_back(sem);
    frameWaitValues_.push_back(waitValue);
    frameWaitStages_.push_back(stage);
}

void VulkanGraphicsBackend::AddFrameSignal(VkSemaphore sem, uint64_t signalValue)
{
    frameSignalSems_.push_back(sem);
    frameSignalValues_.push_back(signalValue);
}

void VulkanGraphicsBackend::AddFramePreCmd(VkCommandBuffer cmd)
{
    framePreCmds_.push_back(cmd);
}

// ── ImGui backend lifecycle ─────────────────────────────────────────────────────

void VulkanGraphicsBackend::ImGuiInitBackends()
{
    ImGui_ImplSDL3_InitForVulkan(window_);

    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = VK_API_VERSION_1_3;
    info.Instance = instance_;
    info.PhysicalDevice = physicalDevice_;
    info.Device = device_;
    info.QueueFamily = graphicsQueueFamily_;
    info.Queue = imguiQueue_; // dedicated multi-viewport queue (see #26)
    info.DescriptorPool = imguiDescriptorPool_;
    info.MinImageCount = minImageCount_;
    info.ImageCount = static_cast<uint32_t>(swapchainImages_.size());
    // Since imgui 2025/09/26 the render pass / MSAA live on the per-pipeline info.
    info.PipelineInfoMain.RenderPass = renderPass_;
    info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ImGui_ImplVulkan_Init(&info);
}

void VulkanGraphicsBackend::ImGuiShutdownBackends()
{
    // Free plugin UI textures while the device + ImGui Vulkan backend are still alive.
    vkDeviceWaitIdle(device_);
    for (const UiTexture& t : uiTextures_)
    {
        ImGui_ImplVulkan_RemoveTexture(t.set);
        vkDestroyImageView(device_, t.view, nullptr);
        vmaDestroyImage(allocator_, t.image, t.alloc);
    }
    uiTextures_.clear();

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
}

void VulkanGraphicsBackend::ImGuiNewFrame()
{
    // Close the video-layer pass (opened in BeginFrame) before ImGui starts a new frame;
    // the UI is recorded straight into the swapchain composite pass in RecordComposite().
    if (recordingVideoLayer_)
    {
        vkCmdEndRenderPass(currentCmd_);
        recordingVideoLayer_ = false;
        videoLayerValid_ = true;
    }
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
}

void VulkanGraphicsBackend::ImGuiRenderDrawData()
{
    // No-op: the main-viewport ImGui draw data is recorded directly into the swapchain
    // composite pass in RecordComposite() (called from SwapBuffers), so the UI draws over
    // the video with no separate offscreen layer. ImGui::GetDrawData() stays valid until
    // the next frame, so deferring the record to SwapBuffers is safe.
}

void VulkanGraphicsBackend::ImGuiRenderPlatformWindows()
{
    // Pop-out panels render into their own OS windows + swapchains, which
    // imgui_impl_vulkan manages internally.
    const ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGui::UpdatePlatformWindows();
        // Render secondary windows only after the main swapchain frame has been
        // submitted/presented. The ImGui Vulkan backend performs its own acquire,
        // fence wait, submit and present here, so guard the queue it was configured
        // to use. With a second graphics queue this does not block FFmpeg/render
        // queue index 0; on single-queue GPUs it serializes correctly.
        VulkanQueueGuard guard(queueLock_, graphicsQueueFamily_, imguiQueueIndex_);
        ImGui::RenderPlatformWindowsDefault();
    }
}

void VulkanGraphicsBackend::ImGuiProcessEvent(const void* sdlEvent)
{
    ImGui_ImplSDL3_ProcessEvent(static_cast<const SDL_Event*>(sdlEvent));
}

void VulkanGraphicsBackend::Shutdown()
{
    if (device_ != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(device_);
    }

    if (imguiDescriptorPool_ != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(device_, imguiDescriptorPool_, nullptr);
    }

    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i)
    {
        vkDestroySemaphore(device_, imageAvailable_[i], nullptr);
        vkDestroyFence(device_, inFlightFences_[i], nullptr);
    }
    for (VkSemaphore s : renderFinished_)
    {
        vkDestroySemaphore(device_, s, nullptr);
    }
    renderFinished_.clear();

    DestroySwapchain(); // also destroys the offscreen layer targets

    // Size-independent composition resources + the layer render pass.
    if (compositeVideoPipeline_ != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device_, compositeVideoPipeline_, nullptr);
    }
    if (compositePipelineLayout_ != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device_, compositePipelineLayout_, nullptr);
    }
    if (compositeDescPool_ != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(device_, compositeDescPool_, nullptr);
    }
    if (compositeSetLayout_ != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(device_, compositeSetLayout_, nullptr);
    }
    if (compositeSampler_ != VK_NULL_HANDLE)
    {
        vkDestroySampler(device_, compositeSampler_, nullptr);
    }
    if (layerRenderPass_ != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(device_, layerRenderPass_, nullptr);
    }

    if (swapchain_ != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    }
    if (renderPass_ != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(device_, renderPass_, nullptr);
    }
    if (commandPool_ != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(device_, commandPool_, nullptr);
    }
    if (allocator_ != nullptr)
    {
        vmaDestroyAllocator(allocator_);
    }
    if (device_ != VK_NULL_HANDLE)
    {
        vkDestroyDevice(device_, nullptr);
    }
    if (surface_ != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
    }
    if (debugMessenger_ != VK_NULL_HANDLE)
    {
        vkDestroyDebugUtilsMessengerEXT(instance_, debugMessenger_, nullptr);
    }
    if (instance_ != VK_NULL_HANDLE)
    {
        vkDestroyInstance(instance_, nullptr);
    }
}
