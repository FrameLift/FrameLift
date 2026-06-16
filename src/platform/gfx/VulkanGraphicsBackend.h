#pragma once

#include <volk.h>

#include <array>
#include <cstdint>
#include <vector>

#include "IGraphicsBackend.h"

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
    [[nodiscard]] VmaAllocator Allocator() const { return allocator_; }
    [[nodiscard]] VkRenderPass RenderPass() const { return renderPass_; }
    [[nodiscard]] VkExtent2D SwapchainExtent() const { return swapchainExtent_; }
    [[nodiscard]] VkCommandBuffer CurrentCommandBuffer() const { return currentCmd_; }
    [[nodiscard]] uint32_t CurrentFrameIndex() const { return currentFrame_; }

    // Record one-shot transfer/setup work on a transient command buffer and block
    // until the GPU finishes. Used by the video renderer to upload frames (Phase 2;
    // replaced by zero-copy in Phase 3).
    bool ImmediateSubmit(void (*record)(VkCommandBuffer cmd, void* ud), void* ud);

private:
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
