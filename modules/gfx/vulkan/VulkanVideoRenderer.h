#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>

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
// upload's blocking transfer barriers against prior fragment-shader reads on the
// graphics queue, so overwriting the shared image is hazard-free.
class VulkanVideoRenderer final : public IVideoRenderer
{
public:
    explicit VulkanVideoRenderer(VulkanGraphicsBackend* backend);
    ~VulkanVideoRenderer() override;

    VulkanVideoRenderer(const VulkanVideoRenderer&) = delete;
    VulkanVideoRenderer& operator=(const VulkanVideoRenderer&) = delete;

    bool Init(IGraphicsBackend* backend) override;
    void Upload(const uint8_t* rgba, int w, int h) override;
    void UploadVulkanFrame(void* avFrame, int displayW, int displayH) override;
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
    // Shared blit-pipeline builder (fullscreen triangle, video.vert/.frag) against a
    // given descriptor-set layout; used for both the RGBA and YCbCr paths.
    bool CreateBlitPipeline(VkDescriptorSetLayout setLayout, VkPipelineLayout& outLayout, VkPipeline& outPipeline);
    VkShaderModule CreateShaderModule(const uint32_t* code, size_t sizeBytes) const;
    bool EnsureTexture(Texture& t, int w, int h);
    void UploadTo(Texture& t, const uint8_t* rgba, int w, int h);
    bool EnsureStaging(VkDeviceSize bytes);
    void DestroyTexture(Texture& t);

    // ── Zero-copy YCbCr sampling (#18) ─────────────────────────────────────────
    // (Re)build the YCbCr conversion + immutable sampler + set layout + pipeline for a
    // decoded VkFormat / colorspace / range; cheap no-op when unchanged.
    bool EnsureYcbcr(int vkFormat, int colorSpace, int colorRange);
    // Immediate teardown (dtor only — requires an idle device).
    void DestroyYcbcr();
    // Deferred teardown for a live rebuild: hands every current YCbCr object (conversion,
    // sampler, layouts, pipeline, descriptor pool, cached views) to the retire queue and
    // nulls the members so EnsureYcbcr can build fresh ones without a device stall.
    void RetireYcbcr();
    bool CreateYcbcrDescPool();

    // Get (or create + cache, keyed by VkImage handle) the view + descriptor set for one
    // pooled decode image. Views/sets live until the format changes or shutdown.
    struct FrameTex
    {
        VkImageView view = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;
    };

    const FrameTex* EnsureFrameTexture(uint64_t image);
    // Drop all cached per-image views/sets (device-idle first: an in-flight frame may
    // still sample them). Used when the decoder's frames pool is replaced and when the
    // descriptor pool is exhausted.
    void InvalidateFrameTextures();
    // Record the frame image's transition (decode→sample layout, queue-ownership acquire)
    // into its own command buffer and register it with the backend to run, in the single
    // per-frame submit, just before the main render CB. Must run OUTSIDE the render pass,
    // hence its own command buffer (Draw runs inside the pass). NOT submitted separately:
    // a standalone submit here would stall the queue ahead of Qt Quick's scene-graph work.
    VkCommandBuffer RecordFrameTransition(uint64_t image, int oldLayout, uint32_t srcQueueFamily);
    // Returns true when it recorded the video draw (and thus set viewport/scissor);
    // false on any early-out, so Draw() knows the overlay must set its own.
    bool DrawVulkanFrame();

    VulkanGraphicsBackend* backend_ = nullptr;
    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
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

    // ── Zero-copy YCbCr state (#18) ────────────────────────────────────────────
    // The AVFrame* (void*) handed in by UploadVulkanFrame, sampled in Draw. Non-null ⇒
    // use the YCbCr path for the video; the RGBA video_ texture is then unused.
    void* vkFrame_ = nullptr;
    int vkDisplayW_ = 0;
    int vkDisplayH_ = 0;

    // Identity of the decoder's hw-frames pool the cached views/sets were built against
    // (VulkanFrameInfo::framesContextId); a change invalidates frameTextures_.
    uint64_t framesContextId_ = 0;

    // Conversion is rebuilt only when the format/colorspace/range changes.
    int ycbcrFormat_ = 0;
    int ycbcrColorSpace_ = -1;
    int ycbcrColorRange_ = -1;
    VkSamplerYcbcrConversion ycbcrConversion_ = VK_NULL_HANDLE;
    VkSampler ycbcrSampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout ycbcrSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout ycbcrPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline ycbcrPipeline_ = VK_NULL_HANDLE;
    VkDescriptorPool ycbcrDescPool_ = VK_NULL_HANDLE;
    // view + descriptor set per pooled decode image (bounded; the decoder reuses a small
    // set of images). Cleared on format change / shutdown — never re-pointed in flight.
    std::unordered_map<uint64_t, FrameTex> frameTextures_;

    // Standalone transition command buffers (one per frame-in-flight), submitted before
    // the main render submit to move the decode image into a sampleable layout.
    VkCommandPool transitionPool_ = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, VulkanGraphicsBackend::kMaxFramesInFlight> transitionCmds_{};
};
