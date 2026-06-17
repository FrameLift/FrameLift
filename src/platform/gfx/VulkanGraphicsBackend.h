#pragma once

#include <volk.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "IGraphicsBackend.h"
#include "VulkanDeviceInfo.h"

struct SDL_Window;

// VMA handles, forward-declared so this header doesn't pull in the (large)
// vk_mem_alloc.h — only the .cpp needs the implementation.
typedef struct VmaAllocator_T* VmaAllocator;
typedef struct VmaAllocation_T* VmaAllocation;

// Vulkan 1.x presentation backend: owns the instance/device/swapchain and drives the
// imgui_impl_vulkan + imgui_impl_sdl3 backends. The single VkInstance/VkDevice/queues
// created here are the spine shared by swapchain present, the ImGui backend, the
// VulkanVideoRenderer, and (in Phase 3) FFmpeg's Vulkan hwaccel.
//
// May #include <SDL3/SDL.h>, volk, VMA and imgui_impl_*.h (same allowance as
// SdlAppWindow). All methods run on the host's main / render thread.
class VulkanGraphicsBackend final : public IGraphicsBackend
{
public:
    static constexpr uint32_t kMaxFramesInFlight = 2;

    // Pre-flight check (before any window is created): is there a usable Vulkan device?
    // Lets the factory fall back to OpenGL instead of failing at startup.
    [[nodiscard]] static bool IsSupported();

    uint64_t PreWindowCreate() override;
    void OnWindowCreated(SDL_Window* window) override;
    void Shutdown() override;
    [[nodiscard]] const char* Name() const override { return "Vulkan"; }

    [[nodiscard]] std::unique_ptr<IVideoRenderer> CreateVideoRenderer() override;
    [[nodiscard]] uintptr_t CreateUiTexture(const unsigned char* rgba, int w, int h) override;

    [[nodiscard]] void* GetProcAddr(const char* name) const override; // not meaningful for Vulkan
    [[nodiscard]] bool GetVulkanDeviceInfo(VulkanDeviceInfo& out) const noexcept override;
    bool BeginFrame() override;
    void SwapBuffers() override;
    void SetVSync(bool enabled) override;

    void ImGuiInitBackends() override;
    void ImGuiShutdownBackends() override;
    void ImGuiNewFrame() override;
    void ImGuiRenderDrawData() override;
    void ImGuiProcessEvent(const void* sdlEvent) override;

    // ── Accessors for the paired VulkanVideoRenderer ───────────────────────────
    [[nodiscard]] VkDevice Device() const { return device_; }
    [[nodiscard]] VkPhysicalDevice PhysicalDevice() const { return physicalDevice_; }
    [[nodiscard]] VmaAllocator Allocator() const { return allocator_; }
    [[nodiscard]] VkRenderPass RenderPass() const { return renderPass_; }
    [[nodiscard]] VkExtent2D SwapchainExtent() const { return swapchainExtent_; }
    [[nodiscard]] VkCommandBuffer CurrentCommandBuffer() const { return currentCmd_; }
    [[nodiscard]] uint32_t CurrentFrameIndex() const { return currentFrame_; }
    [[nodiscard]] VkQueue GraphicsQueue() const { return graphicsQueue_; }
    [[nodiscard]] uint32_t GraphicsQueueFamily() const { return graphicsQueueFamily_; }
    [[nodiscard]] bool SupportsVulkanVideoDecode() const { return supportsVulkanVideo_; }

    // Register a decoded AVVkFrame's timeline semaphore with the current frame's queue
    // submission (added in SwapBuffers). The renderer calls these from Draw() after
    // recording the sample of a zero-copy Vulkan frame: wait until the decode signalled
    // `waitValue`, and signal `signalValue` (= waitValue + 1) when sampling completes.
    // Cleared each BeginFrame.
    void AddFrameWait(VkSemaphore sem, uint64_t waitValue, VkPipelineStageFlags stage);
    void AddFrameSignal(VkSemaphore sem, uint64_t signalValue);

    // Record one-shot transfer/setup work on a transient command buffer and block
    // until the GPU finishes. Used by the video renderer to upload frames (Phase 2;
    // replaced by zero-copy in Phase 3).
    bool ImmediateSubmit(void (*record)(VkCommandBuffer cmd, void* ud), void* ud);

private:
    // Find a VK_QUEUE_VIDEO_DECODE_BIT_KHR queue family on physicalDevice_ and record its
    // index + codec caps + flags (and the graphics family's flags). Clears
    // supportsVulkanVideo_ if no decode queue actually exists.
    void DetectVideoDecodeQueue();

    bool CreateSwapchain();
    void DestroySwapchain();
    bool RecreateSwapchain();
    bool CreateRenderPass();
    bool CreateFramebuffers();
    bool CreateSyncObjects();

    SDL_Window* window_ = nullptr;
    bool shown_ = false;  // created hidden; shown on the first successful present
    bool vsync_ = false;  // FIFO when true, MAILBOX/IMMEDIATE when false

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamily_ = 0;
    VmaAllocator allocator_ = nullptr;

    // ── Vulkan-video decode device state (Phase 3, #18) ────────────────────────
    // Populated only when the physical device exposes a video-decode queue + the
    // required extensions; otherwise supportsVulkanVideo_ stays false and the player
    // falls back to the CPU-RGBA8 path. The recorded extension/feature lists are handed
    // verbatim to FFmpeg via GetVulkanDeviceInfo so it WRAPS this device.
    bool supportsVulkanVideo_ = false;
    VkQueue videoDecodeQueue_ = VK_NULL_HANDLE;
    int videoDecodeQueueFamily_ = -1;
    uint32_t videoDecodeCaps_ = 0;       // VkVideoCodecOperationFlagBitsKHR of the decode family
    uint32_t graphicsQueueFlags_ = 0;    // VkQueueFlagBits of the graphics family
    uint32_t videoDecodeQueueFlags_ = 0; // VkQueueFlagBits of the decode family
    std::vector<std::string> enabledDeviceExtNames_;
    std::vector<const char*> enabledDeviceExtPtrs_; // points into enabledDeviceExtNames_
    // Enabled feature chain (lifetime spans device usage; FFmpeg reads it at init).
    VkPhysicalDeviceFeatures2 enabledFeatures2_{};
    VkPhysicalDeviceVulkan11Features enabledF11_{};
    VkPhysicalDeviceVulkan12Features enabledF12_{};
    VkPhysicalDeviceVulkan13Features enabledF13_{};

    // Per-submission timeline waits/signals for the zero-copy video frame sampled this
    // frame (parallel arrays consumed by SwapBuffers, reset by BeginFrame).
    std::vector<VkSemaphore> frameWaitSems_;
    std::vector<uint64_t> frameWaitValues_;
    std::vector<VkPipelineStageFlags> frameWaitStages_;
    std::vector<VkSemaphore> frameSignalSems_;
    std::vector<uint64_t> frameSignalValues_;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapchainFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchainExtent_{};
    uint32_t minImageCount_ = 0;
    std::vector<VkImage> swapchainImages_;
    std::vector<VkImageView> swapchainImageViews_;
    std::vector<VkFramebuffer> framebuffers_;

    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, kMaxFramesInFlight> commandBuffers_{};
    std::array<VkSemaphore, kMaxFramesInFlight> imageAvailable_{};
    std::array<VkFence, kMaxFramesInFlight> inFlightFences_{};
    std::vector<VkSemaphore> renderFinished_; // per swapchain image
    std::vector<VkFence> imagesInFlight_;     // per swapchain image (non-owning copies)

    VkDescriptorPool imguiDescriptorPool_ = VK_NULL_HANDLE;

    // ImGui-usable textures (plugin icons via CreateUiTexture), freed on shutdown.
    struct UiTexture
    {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation alloc = nullptr;
        VkImageView view = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;
    };
    std::vector<UiTexture> uiTextures_;

    uint32_t currentFrame_ = 0;
    uint32_t imageIndex_ = 0;
    VkCommandBuffer currentCmd_ = VK_NULL_HANDLE;
    bool frameActive_ = false;
};
