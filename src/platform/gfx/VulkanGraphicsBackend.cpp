#include "VulkanGraphicsBackend.h"

#include "VulkanVideoRenderer.h"
#include "util.h"

// VMA lives in this TU. Use volk's dynamically loaded entry points (no static link),
// letting VMA pull the rest from the two getters we supply below.
#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <vk_mem_alloc.h>

#include <VkBootstrap.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_vulkan.h"

#include <framelift/Log.h>

#include <cstring>
#include <optional>

namespace
{
#ifndef NDEBUG
constexpr bool kEnableValidation = true;
#else
constexpr bool kEnableValidation = false;
#endif

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
    std::vector<const char*> extensions(sdlExts, sdlExts + sdlExtCount);

    vkb::InstanceBuilder instanceBuilder;
    // 1.3 instance: required by FFmpeg's Vulkan hwaccel (AVVulkanDeviceContext). The
    // device may still come back at 1.1 on older GPUs (video unsupported → CPU fallback).
    instanceBuilder.set_app_name("FrameLift").require_api_version(1, 3, 0).enable_extensions(extensions);
    if (kEnableValidation)
    {
        instanceBuilder.request_validation_layers(true).enable_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    auto instanceRet = instanceBuilder.build();
    if (!instanceRet)
    {
        Fatal(("Vulkan instance creation failed: " + instanceRet.error().message()).c_str());
    }
    vkb::Instance vkbInstance = instanceRet.value();
    instance_ = vkbInstance.instance;
    volkLoadInstance(instance_);

    if (kEnableValidation)
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

    struct DevicePick
    {
        vkb::PhysicalDevice phys;
        vkb::Device device;
    };
    auto tryBuild = [&](bool withVideo) -> std::optional<DevicePick> {
        vkb::PhysicalDeviceSelector sel(vkbInstance);
        sel.set_surface(surface_);
        if (withVideo)
        {
            sel.set_minimum_version(1, 3)
                .set_required_features(enabledFeatures2_.features)
                .set_required_features_11(enabledF11_)
                .set_required_features_12(enabledF12_)
                .set_required_features_13(enabledF13_)
                .add_required_extension(VK_KHR_VIDEO_QUEUE_EXTENSION_NAME)
                .add_required_extension(VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME);
        }
        else
        {
            sel.set_minimum_version(1, 1);
        }
        auto pr = sel.select();
        if (!pr)
        {
            return std::nullopt;
        }
        vkb::PhysicalDevice phys = pr.value();
        if (withVideo)
        {
            // Codec + maintenance extensions vary by GPU; enable whichever are present.
            phys.enable_extension_if_present(VK_KHR_VIDEO_DECODE_H264_EXTENSION_NAME);
            phys.enable_extension_if_present(VK_KHR_VIDEO_DECODE_H265_EXTENSION_NAME);
            phys.enable_extension_if_present(VK_KHR_VIDEO_DECODE_AV1_EXTENSION_NAME);
            phys.enable_extension_if_present(VK_KHR_VIDEO_MAINTENANCE_1_EXTENSION_NAME);
        }
        vkb::DeviceBuilder db(phys);
        auto dr = db.build();
        if (!dr)
        {
            return std::nullopt;
        }
        return DevicePick{phys, dr.value()};
    };

    auto pick = tryBuild(true);
    if (pick)
    {
        supportsVulkanVideo_ = true;
    }
    else
    {
        Log::Info("Vulkan: video-decode device unavailable; using a plain render device (CPU upload path)");
        pick = tryBuild(false);
    }
    if (!pick)
    {
        Fatal("Vulkan device creation failed");
    }

    physicalDevice_ = pick->phys.physical_device;
    vkb::Device vkbDevice = pick->device;
    device_ = vkbDevice.device;
    volkLoadDevice(device_);

    graphicsQueue_ = vkbDevice.get_queue(vkb::QueueType::graphics).value();
    graphicsQueueFamily_ = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();
    presentQueue_ = vkbDevice.get_queue(vkb::QueueType::present).value();

    // Record the queue flags + (if video) detect the decode queue family and its codec
    // caps, so GetVulkanDeviceInfo can hand FFmpeg a complete qf[] list.
    if (supportsVulkanVideo_)
    {
        DetectVideoDecodeQueue();
    }
    if (!supportsVulkanVideo_)
    {
        // tryBuild(true) may succeed on a device that lacks an actual decode queue;
        // DetectVideoDecodeQueue() clears the flag in that case. Still record graphics
        // flags for completeness.
        uint32_t qfCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &qfCount, nullptr);
        std::vector<VkQueueFamilyProperties> qfp(qfCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &qfCount, qfp.data());
        if (graphicsQueueFamily_ < qfCount)
        {
            graphicsQueueFlags_ = qfp[graphicsQueueFamily_].queueFlags;
        }
    }

    // Link the feature chain now (after vk-bootstrap copied the unchained structs) so
    // GetVulkanDeviceInfo can hand FFmpeg the full VkPhysicalDeviceFeatures2 -> 11/12/13 chain.
    enabledFeatures2_.pNext = &enabledF11_;
    enabledF11_.pNext = &enabledF12_;
    enabledF12_.pNext = &enabledF13_;
    enabledF13_.pNext = nullptr;

    // Record the enabled device-extension list (verbatim) for FFmpeg's wrap.
    enabledDeviceExtNames_ = pick->phys.get_extensions();
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

    if (!CreateSwapchain() || !CreateRenderPass() || !CreateFramebuffers() || !CreateSyncObjects())
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
    int pw = 0, ph = 0;
    SDL_GetWindowSizeInPixels(window_, &pw, &ph);

    vkb::SwapchainBuilder builder(physicalDevice_, device_, surface_, graphicsQueueFamily_, graphicsQueueFamily_);
    builder.set_desired_format(VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
        .set_desired_present_mode(vsync_ ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_MAILBOX_KHR)
        .set_desired_extent(static_cast<uint32_t>(pw), static_cast<uint32_t>(ph))
        .set_old_swapchain(swapchain_);
    auto ret = builder.build();
    if (!ret)
    {
        Log::Error("Vulkan swapchain build failed: {}", ret.error().message());
        return false;
    }
    vkb::Swapchain sc = ret.value();
    swapchain_ = sc.swapchain;
    swapchainFormat_ = sc.image_format;
    swapchainExtent_ = sc.extent;
    swapchainImages_ = sc.get_images().value();
    swapchainImageViews_ = sc.get_image_views().value();
    // ImGui's MinImageCount must be >= 2 and <= ImageCount; the actual count satisfies both.
    minImageCount_ = static_cast<uint32_t>(swapchainImages_.size());
    return true;
}

bool VulkanGraphicsBackend::CreateRenderPass()
{
    VkAttachmentDescription color{};
    color.format = swapchainFormat_;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; // clear to black each frame (letterbox bars)
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

    const bool ok = CreateSwapchain() && CreateFramebuffers();
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

    currentCmd_ = commandBuffers_[currentFrame_];
    vkResetCommandBuffer(currentCmd_, 0);

    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(currentCmd_, &begin);

    VkClearValue clear{};
    clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass = renderPass_;
    rp.framebuffer = framebuffers_[imageIndex_];
    rp.renderArea.extent = swapchainExtent_;
    rp.clearValueCount = 1;
    rp.pClearValues = &clear;
    vkCmdBeginRenderPass(currentCmd_, &rp, VK_SUBPASS_CONTENTS_INLINE);

    frameActive_ = true;
    return true;
}

void VulkanGraphicsBackend::SwapBuffers()
{
    if (!frameActive_)
    {
        return;
    }
    frameActive_ = false;

    vkCmdEndRenderPass(currentCmd_);
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

    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.pNext = hasTimeline ? &timeline : nullptr;
    submit.waitSemaphoreCount = static_cast<uint32_t>(waitSems.size());
    submit.pWaitSemaphores = waitSems.data();
    submit.pWaitDstStageMask = waitStages.data();
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &currentCmd_;
    submit.signalSemaphoreCount = static_cast<uint32_t>(signalSems.size());
    submit.pSignalSemaphores = signalSems.data();
    vkQueueSubmit(graphicsQueue_, 1, &submit, inFlightFences_[currentFrame_]);

    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &renderFinished_[imageIndex_];
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain_;
    present.pImageIndices = &imageIndex_;
    VkResult pres = vkQueuePresentKHR(presentQueue_, &present);
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

void VulkanGraphicsBackend::SetVSync(bool enabled)
{
    if (vsync_ == enabled)
    {
        return;
    }
    vsync_ = enabled;
    RecreateSwapchain(); // present mode is baked into the swapchain
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
    vkQueueSubmit(graphicsQueue_, 1, &submit, fence);
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

    // vk-bootstrap's default queue setup creates one queue per family, so the decode
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
    info.Queue = graphicsQueue_;
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
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
}

void VulkanGraphicsBackend::ImGuiRenderDrawData()
{
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), currentCmd_);

    // Pop-out panels render into their own OS windows + swapchains, which
    // imgui_impl_vulkan manages internally.
    const ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGui::UpdatePlatformWindows();
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

    DestroySwapchain();
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
