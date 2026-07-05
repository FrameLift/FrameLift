#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>

#include "IVideoRenderer.h"
#include "VulkanGraphicsBackend.h"
#include "VulkanTextureRing.h"

// VMA handle, forward-declared (impl lives in VulkanGraphicsBackend.cpp).
typedef struct VmaAllocation_T* VmaAllocation;

// Vulkan blitter: uploads software-decoded RGBA frames to a sampled VkImage and draws
// them, letterboxed, into the active swapchain render pass the host also draws the UI
// into — the Vulkan analogue of GlVideoRenderer. Created by
// VulkanGraphicsBackend::CreateVideoRenderer(), so it holds the concrete backend for
// the device/allocator/render-pass and the per-frame command buffer.
//
// A single persistent image per stream (video + overlay), like GlVideoRenderer, so the
// last uploaded frame stays on screen across presents (paused / low-fps content).
// Uploads record into the backend's per-frame frame-ops command buffer (no blocking
// submit); the copy's barriers chain against prior frames' fragment-shader reads via
// queue submission order, so overwriting the shared image is hazard-free.
class VulkanVideoRenderer final : public IVideoRenderer
{
public:
    explicit VulkanVideoRenderer(VulkanGraphicsBackend* backend);
    ~VulkanVideoRenderer() override;

    VulkanVideoRenderer(const VulkanVideoRenderer&) = delete;
    VulkanVideoRenderer& operator=(const VulkanVideoRenderer&) = delete;

    bool Init(IGraphicsBackend* backend) override;
    void UploadFrame(const uint8_t* data, const VideoFrameDesc& desc) override;
    void UploadVulkanFrame(void* avFrame, int displayW, int displayH) override;
    void UploadOverlay(const uint8_t* rgba, int w, int h) override;
    void Draw(int fbW, int fbH, bool drawOverlay = false) override;

private:
    // One sampled frame: 1 (RGBA), 2 (NV12) or 3 (I420) plane images sharing a single
    // descriptor set. All planes move through layout transitions together, so one
    // `layout` covers the group.
    struct Texture
    {
        VkImage image[3] = {VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE};
        VmaAllocation alloc[3] = {nullptr, nullptr, nullptr};
        VkImageView view[3] = {VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE};
        VkDescriptorSet set = VK_NULL_HANDLE;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        int w = 0;
        int h = 0;
        VideoPixelFormat format = VideoPixelFormat::RGBA;
        bool valid = false;
    };

    // Host-image-copy upload target: a ring of sampled images the CPU writes directly
    // (no staging, no barriers, no submit). Ring size covers the worst-case frames in
    // flight plus the steady-state slack the policy needs (see VulkanTextureRing.h).
    struct HostRing
    {
        static constexpr uint32_t kSlots = VulkanGraphicsBackend::kMaxFramesInFlight + 2;
        std::array<Texture, kSlots> tex{};
        VulkanTextureRing policy;
    };

    // Per-frame-slot staging arena: each Qt frame slot owns a growable, persistently
    // mapped buffer that uploads bump-allocate from (video + overlay share it within a
    // frame). CPU reuse is safe because Qt waits the slot's fence before the slot comes
    // around again, which covers the frame-ops submit that read the buffer.
    struct StagingSlot
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation alloc = nullptr;
        void* mapped = nullptr;
        VkDeviceSize size = 0;
        VkDeviceSize used = 0;
        uint64_t frame = 0; // backend FrameCounter() of last use; `used` resets on change
    };

    bool BuildPipeline();
    // Shared blit-pipeline builder (fullscreen triangle, video.vert + the given
    // fragment SPIR-V — video.frag when null) against a given descriptor-set layout;
    // used by the RGBA, planar-YUV and YCbCr paths. pushConstantBytes > 0 adds a
    // fragment-stage push-constant range to the layout.
    bool CreateBlitPipeline(
        VkDescriptorSetLayout setLayout, VkPipelineLayout& outLayout, VkPipeline& outPipeline,
        const uint32_t* fragCode = nullptr, size_t fragSizeBytes = 0, uint32_t pushConstantBytes = 0
    );
    VkShaderModule CreateShaderModule(const uint32_t* code, size_t sizeBytes) const;
    // hostCopy selects HOST_TRANSFER usage + host-copy sampling layout instead of the
    // staging path's TRANSFER_DST + SHADER_READ_ONLY.
    bool EnsureTexture(Texture& t, const VideoFrameDesc& desc, bool hostCopy);
    void UploadTo(Texture& t, const uint8_t* data, const VideoFrameDesc& desc);
    void UploadHostCopy(HostRing& ring, const uint8_t* data, const VideoFrameDesc& desc);
    // Currently displayable texture for a stream: the ring's displayed slot on the
    // host-copy path, the single texture otherwise; null when nothing was uploaded yet.
    const Texture* CurrentTexture(HostRing& ring, const Texture& single) const;
    // Make room for `bytes` more in the slot's arena, growing (old buffer retired — a
    // recorded copy may still reference it) when needed; growth resets `used` because
    // fresh space starts at offset 0 of the new buffer.
    bool EnsureStagingSpace(StagingSlot& slot, VkDeviceSize bytes);
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
    // Drop all cached per-image views/sets (retired, not destroyed — an in-flight frame
    // may still sample them). Used when the decoder's frames pool is replaced and when
    // the descriptor pool is exhausted.
    void InvalidateFrameTextures();
    // Record the frame image's transition (decode→sample layout, queue-ownership
    // acquire) into the backend's per-frame frame-ops command buffer, which is submitted
    // once — together with the accumulated timeline waits — just before Qt's scene-graph
    // submit. It must run outside the render pass Draw records into, hence the separate
    // command buffer; batching it there avoids a standalone submit stalling the queue.
    bool RecordFrameTransition(uint64_t image, int oldLayout, uint32_t srcQueueFamily);
    // Returns true when it recorded the video draw (and thus set viewport/scissor);
    // false on any early-out, so Draw() knows the overlay must set its own.
    bool DrawVulkanFrame();

    VulkanGraphicsBackend* backend_ = nullptr;
    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = nullptr;

    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;    // 1 binding (RGBA video + overlay)
    VkDescriptorSetLayout yuvSetLayout_ = VK_NULL_HANDLE; // 3 bindings (Y / U-or-UV / V)
    VkDescriptorPool descPool_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout yuvPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline yuvPipeline_ = VK_NULL_HANDLE;

    // Push-constant block for video_yuv.frag (4 vec4: matrix columns + bias/nv12),
    // rebaked in UploadFrame only when the colourimetry changes.
    float yuvPush_[16] = {};
    int yuvPushColorspace_ = -1;
    int yuvPushFullRange_ = -1;
    int yuvPushHeight_ = -1;

    // Staging-path textures (one per stream)…
    Texture video_{};
    Texture overlay_{};
    // …or host-copy rings when the backend supports host image copy (decided in Init).
    bool useHostCopy_ = false;
    // Push per-frame YCbCr views into the command buffer instead of pool-allocated sets.
    bool usePushDesc_ = false;
    HostRing videoRing_{};
    HostRing overlayRing_{};

    std::array<StagingSlot, VulkanGraphicsBackend::kMaxFramesInFlight> staging_{};

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
};
