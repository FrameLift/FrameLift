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
} // namespace

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
    instanceBuilder.set_app_name("FrameLift").require_api_version(1, 1, 0).enable_extensions(extensions);
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
    vkb::PhysicalDeviceSelector selector(vkbInstance);
    auto physRet = selector.set_surface(surface_).set_minimum_version(1, 1).select();
    if (!physRet)
    {
        Fatal(("No suitable Vulkan device: " + physRet.error().message()).c_str());
    }
    physicalDevice_ = physRet.value().physical_device;

    vkb::DeviceBuilder deviceBuilder(physRet.value());
    auto deviceRet = deviceBuilder.build();
    if (!deviceRet)
    {
        Fatal(("Vulkan device creation failed: " + deviceRet.error().message()).c_str());
    }
    vkb::Device vkbDevice = deviceRet.value();
    device_ = vkbDevice.device;
    volkLoadDevice(device_);

    graphicsQueue_ = vkbDevice.get_queue(vkb::QueueType::graphics).value();
    graphicsQueueFamily_ = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();
    presentQueue_ = vkbDevice.get_queue(vkb::QueueType::present).value();

    // ── VMA allocator (driven by volk's loaded pointers) ───────────────────────
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

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &imageAvailable_[currentFrame_];
    submit.pWaitDstStageMask = &waitStage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &currentCmd_;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &renderFinished_[imageIndex_];
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

// ── ImGui backend lifecycle ─────────────────────────────────────────────────────

void VulkanGraphicsBackend::ImGuiInitBackends()
{
    ImGui_ImplSDL3_InitForVulkan(window_);

    ImGui_ImplVulkan_InitInfo info{};
    info.Instance = instance_;
    info.PhysicalDevice = physicalDevice_;
    info.Device = device_;
    info.QueueFamily = graphicsQueueFamily_;
    info.Queue = graphicsQueue_;
    info.DescriptorPool = imguiDescriptorPool_;
    info.RenderPass = renderPass_;
    info.MinImageCount = minImageCount_;
    info.ImageCount = static_cast<uint32_t>(swapchainImages_.size());
    info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ImGui_ImplVulkan_Init(&info);
}

void VulkanGraphicsBackend::ImGuiShutdownBackends()
{
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
