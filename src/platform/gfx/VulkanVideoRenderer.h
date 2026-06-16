#pragma once

#include <volk.h>

#include <cstdint>

#include "IVideoRenderer.h"
#include "VulkanGraphicsBackend.h"

// VMA handle, forward-declared (impl lives in VulkanGraphicsBackend.cpp).
typedef struct VmaAllocation_T* VmaAllocation;

// Vulkan blitter: uploads software-decoded RGBA frames to a sampled VkImage and draws
// them, letterboxed, into the active swapchain render pass the host also draws the UI
// into — the Vulkan analogue of GlVideoRenderer. Created by
// VulkanGraphicsBackend::CreateVideoRenderer(), so it holds the concrete backend for
// the device/allocator/render-pass and the per-frame command buffer.
//
// A single persistent image per stream (video + overlay), like GlVideoRenderer, so the
// last uploaded frame stays on screen across presents (paused / low-fps content). The
// upload's blocking transfer (Phase 2 parity; zero-copy in Phase 3) barriers against
// prior fragment-shader reads on the graphics queue, so overwriting is hazard-free.
class VulkanVideoRenderer final : public IVideoRenderer
{
public:
    explicit VulkanVideoRenderer(VulkanGraphicsBackend* backend);
    ~VulkanVideoRenderer() override;

    VulkanVideoRenderer(const VulkanVideoRenderer&) = delete;
    VulkanVideoRenderer& operator=(const VulkanVideoRenderer&) = delete;

    bool Init(IGraphicsBackend* backend) override;
    void Upload(const uint8_t* rgba, int w, int h) override;
    void UploadOverlay(const uint8_t* rgba, int w, int h) override;
    void Draw(int fbW, int fbH, bool drawOverlay = false) override;

private:
    // One sampled RGBA image (+ its view and descriptor set) per frame-in-flight slot.
    struct Texture
    {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation alloc = nullptr;
        VkImageView view = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        int w = 0;
        int h = 0;
        bool valid = false;
    };

    bool BuildPipeline();
    VkShaderModule CreateShaderModule(const uint32_t* code, size_t sizeBytes) const;
    bool EnsureTexture(Texture& t, int w, int h);
    void UploadTo(Texture& t, const uint8_t* rgba, int w, int h);
    bool EnsureStaging(VkDeviceSize bytes);
    void DestroyTexture(Texture& t);

    VulkanGraphicsBackend* backend_ = nullptr;
    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = nullptr;

    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descPool_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;

    Texture video_{};
    Texture overlay_{};

    // Growable host-visible staging buffer (uploads are serialized by ImmediateSubmit).
    VkBuffer staging_ = VK_NULL_HANDLE;
    VmaAllocation stagingAlloc_ = nullptr;
    void* stagingMapped_ = nullptr;
    VkDeviceSize stagingSize_ = 0;
};
