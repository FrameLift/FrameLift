#include "VulkanVideoRenderer.h"

#include "FFmpegLetterbox.h"
#include "FFmpegVulkanDevice.h" // neutral bridge (no libav types) — reads AVVkFrame
#include "VulkanColorMapping.h"
#include "VulkanUtil.h"

#include <framelift/Log.h>

#include <vector>

// Zero-copy YCbCr sampling (#18): everything that touches the decoder's pooled
// AVVkFrame images — the sampler-YCbCr conversion bundle, the per-image view/set
// cache, the decode→sample transition and the letterboxed draw. The RGBA upload
// paths live in VulkanVideoRenderer.cpp.

// The Vulkan-free mapping table (VulkanColorMapping.h, unit-tested) mirrors these
// enum values; prove the mirror before casting.
static_assert(VulkanColorMapping::kModelYcbcr709 == VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_709);
static_assert(VulkanColorMapping::kModelYcbcr601 == VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_601);
static_assert(VulkanColorMapping::kModelYcbcr2020 == VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_2020);
static_assert(VulkanColorMapping::kRangeItuFull == VK_SAMPLER_YCBCR_RANGE_ITU_FULL);
static_assert(VulkanColorMapping::kRangeItuNarrow == VK_SAMPLER_YCBCR_RANGE_ITU_NARROW);

void VulkanVideoRenderer::DestroyYcbcr()
{
    for (auto& kv : frameTextures_)
    {
        if (kv.second.view != VK_NULL_HANDLE)
        {
            vkDestroyImageView(device_, kv.second.view, nullptr);
        }
    }
    frameTextures_.clear();

    if (ycbcrPipeline_ != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device_, ycbcrPipeline_, nullptr);
        ycbcrPipeline_ = VK_NULL_HANDLE;
    }
    if (ycbcrPipelineLayout_ != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device_, ycbcrPipelineLayout_, nullptr);
        ycbcrPipelineLayout_ = VK_NULL_HANDLE;
    }
    if (ycbcrDescPool_ != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(device_, ycbcrDescPool_, nullptr);
        ycbcrDescPool_ = VK_NULL_HANDLE;
    }
    if (ycbcrSetLayout_ != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(device_, ycbcrSetLayout_, nullptr);
        ycbcrSetLayout_ = VK_NULL_HANDLE;
    }
    if (ycbcrSampler_ != VK_NULL_HANDLE)
    {
        vkDestroySampler(device_, ycbcrSampler_, nullptr);
        ycbcrSampler_ = VK_NULL_HANDLE;
    }
    if (ycbcrConversion_ != VK_NULL_HANDLE)
    {
        vkDestroySamplerYcbcrConversion(device_, ycbcrConversion_, nullptr);
        ycbcrConversion_ = VK_NULL_HANDLE;
    }
    ycbcrFormat_ = 0;
    ycbcrColorSpace_ = -1;
    ycbcrColorRange_ = -1;
}

void VulkanVideoRenderer::RetireYcbcr()
{
    if (ycbcrConversion_ == VK_NULL_HANDLE && ycbcrPipeline_ == VK_NULL_HANDLE && frameTextures_.empty())
    {
        return; // nothing built yet (first EnsureYcbcr)
    }
    std::vector<VkImageView> views;
    views.reserve(frameTextures_.size());
    for (auto& kv : frameTextures_)
    {
        views.push_back(kv.second.view);
    }
    frameTextures_.clear();

    backend_->Retire(
        [device = device_, views = std::move(views), pipeline = ycbcrPipeline_, layout = ycbcrPipelineLayout_,
         pool = ycbcrDescPool_, setLayout = ycbcrSetLayout_, sampler = ycbcrSampler_, conversion = ycbcrConversion_]
        {
            for (VkImageView view : views)
            {
                vkDestroyImageView(device, view, nullptr);
            }
            if (pipeline != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(device, pipeline, nullptr);
            }
            if (layout != VK_NULL_HANDLE)
            {
                vkDestroyPipelineLayout(device, layout, nullptr);
            }
            if (pool != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorPool(device, pool, nullptr); // frees the cached sets with it
            }
            if (setLayout != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
            }
            if (sampler != VK_NULL_HANDLE)
            {
                vkDestroySampler(device, sampler, nullptr);
            }
            if (conversion != VK_NULL_HANDLE)
            {
                vkDestroySamplerYcbcrConversion(device, conversion, nullptr);
            }
        }
    );
    ycbcrPipeline_ = VK_NULL_HANDLE;
    ycbcrPipelineLayout_ = VK_NULL_HANDLE;
    ycbcrDescPool_ = VK_NULL_HANDLE;
    ycbcrSetLayout_ = VK_NULL_HANDLE;
    ycbcrSampler_ = VK_NULL_HANDLE;
    ycbcrConversion_ = VK_NULL_HANDLE;
    ycbcrFormat_ = 0;
    ycbcrColorSpace_ = -1;
    ycbcrColorRange_ = -1;
}

bool VulkanVideoRenderer::CreateYcbcrDescPool()
{
    // One descriptor set per pooled decode image (decoder reuses a small bounded set).
    constexpr uint32_t kMaxFrameSets = 16;
    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxFrameSets};
    VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pi.maxSets = kMaxFrameSets;
    pi.poolSizeCount = 1;
    pi.pPoolSizes = &ps;
    VK_CHECK_LOG_RETURN(
        vkCreateDescriptorPool(device_, &pi, nullptr, &ycbcrDescPool_),
        "VulkanVideoRenderer: YCbCr descriptor pool creation failed", false
    );
    return true;
}

bool VulkanVideoRenderer::EnsureYcbcr(int vkFormat, int colorSpace, int colorRange)
{
    if (ycbcrConversion_ != VK_NULL_HANDLE && ycbcrFormat_ == vkFormat && ycbcrColorSpace_ == colorSpace &&
        ycbcrColorRange_ == colorRange)
    {
        return true; // already built for this format/colourimetry
    }

    // Rebuild; views/sets baked against the old conversion must go too. In-flight frames
    // may still use the old objects, so they are retired rather than destroyed in place.
    RetireYcbcr();

    const auto fmt = static_cast<VkFormat>(vkFormat);

    // Pick chroma reconstruction settings the format actually supports.
    VkFormatProperties fp{};
    vkGetPhysicalDeviceFormatProperties(physicalDevice_, fmt, &fp);
    const VkFormatFeatureFlags feat = fp.optimalTilingFeatures;
    const VkFilter chromaFilter = (feat & VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_LINEAR_FILTER_BIT)
                                      ? VK_FILTER_LINEAR
                                      : VK_FILTER_NEAREST;
    const VkChromaLocation xChroma = (feat & VK_FORMAT_FEATURE_COSITED_CHROMA_SAMPLES_BIT)
                                         ? VK_CHROMA_LOCATION_COSITED_EVEN
                                         : VK_CHROMA_LOCATION_MIDPOINT;

    VkSamplerYcbcrConversionCreateInfo cci{VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO};
    cci.format = fmt;
    cci.ycbcrModel = static_cast<VkSamplerYcbcrModelConversion>(VulkanColorMapping::ModelFromAvColorSpace(colorSpace));
    cci.ycbcrRange = static_cast<VkSamplerYcbcrRange>(VulkanColorMapping::RangeFromAvColorRange(colorRange));
    cci.components = {
        VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY
    };
    cci.xChromaOffset = xChroma;
    cci.yChromaOffset = xChroma;
    cci.chromaFilter = chromaFilter;
    cci.forceExplicitReconstruction = VK_FALSE;
    if (vkCreateSamplerYcbcrConversion(device_, &cci, nullptr, &ycbcrConversion_) != VK_SUCCESS)
    {
        Log::Error("VulkanVideoRenderer: YCbCr conversion creation failed (fmt {})", vkFormat);
        return false;
    }

    VkSamplerYcbcrConversionInfo convInfo{VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO};
    convInfo.conversion = ycbcrConversion_;

    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.pNext = &convInfo;
    si.magFilter = chromaFilter;
    si.minFilter = chromaFilter;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    if (vkCreateSampler(device_, &si, nullptr, &ycbcrSampler_) != VK_SUCCESS)
    {
        Log::Error("VulkanVideoRenderer: YCbCr sampler creation failed");
        return false;
    }

    // Immutable-sampler binding: a YCbCr-conversion sampler MUST be immutable in the layout.
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    binding.pImmutableSamplers = &ycbcrSampler_;
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    if (usePushDesc_)
    {
        li.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    }
    li.bindingCount = 1;
    li.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(device_, &li, nullptr, &ycbcrSetLayout_) != VK_SUCCESS)
    {
        Log::Error("VulkanVideoRenderer: YCbCr set layout creation failed");
        return false;
    }

    // Push descriptors bind the per-frame view straight into the command buffer, so
    // the pool (and its exhaustion/invalidation handling) exists only on the fallback.
    if (!usePushDesc_ && !CreateYcbcrDescPool())
    {
        return false;
    }

    if (!CreateBlitPipeline(ycbcrSetLayout_, ycbcrPipelineLayout_, ycbcrPipeline_))
    {
        return false;
    }
    VulkanUtil::SetObjectName(device_, VK_OBJECT_TYPE_PIPELINE, ycbcrPipeline_, "FrameLift YCbCr blit pipeline");
    VulkanUtil::SetObjectName(device_, VK_OBJECT_TYPE_SAMPLER, ycbcrSampler_, "FrameLift YCbCr sampler");

    ycbcrFormat_ = vkFormat;
    ycbcrColorSpace_ = colorSpace;
    ycbcrColorRange_ = colorRange;
    return true;
}

void VulkanVideoRenderer::InvalidateFrameTextures()
{
    if (frameTextures_.empty())
    {
        return;
    }
    // In-flight frames may still reference the views/sets, so the whole pool is retired
    // (sets die with it) and replaced by a fresh one — no reset-in-place, no stall.
    std::vector<VkImageView> views;
    views.reserve(frameTextures_.size());
    for (auto& kv : frameTextures_)
    {
        views.push_back(kv.second.view);
    }
    frameTextures_.clear();
    backend_->Retire(
        [device = device_, views = std::move(views), pool = ycbcrDescPool_]
        {
            for (VkImageView view : views)
            {
                vkDestroyImageView(device, view, nullptr);
            }
            if (pool != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorPool(device, pool, nullptr);
            }
        }
    );
    ycbcrDescPool_ = VK_NULL_HANDLE;
    if (!usePushDesc_)
    {
        CreateYcbcrDescPool();
    }
}

const VulkanVideoRenderer::FrameTex* VulkanVideoRenderer::EnsureFrameTexture(uint64_t image)
{
    if (auto it = frameTextures_.find(image); it != frameTextures_.end())
    {
        return &it->second;
    }
    if (frameTextures_.size() >= 16)
    {
        // Pool exhausted (shouldn't happen — bounded decode pool). Drop the cache and
        // wait so the views/sets can be rebuilt safely.
        InvalidateFrameTextures();
    }

    FrameTex ft{};

    VkSamplerYcbcrConversionInfo convInfo{VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO};
    convInfo.conversion = ycbcrConversion_;
    // FFmpeg allocates the decode images with usage flags (e.g. STORAGE) the multi-plane
    // format may not support for optimal tiling; restrict the view to sampling only so it
    // doesn't inherit them (VUID-VkImageViewCreateInfo-usage-02275).
    VkImageViewUsageCreateInfo usageInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO};
    usageInfo.pNext = &convInfo;
    usageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.pNext = &usageInfo;
    vci.image = reinterpret_cast<VkImage>(static_cast<uintptr_t>(image));
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = static_cast<VkFormat>(ycbcrFormat_);
    vci.components = {
        VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY
    };
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(device_, &vci, nullptr, &ft.view) != VK_SUCCESS)
    {
        Log::Error("VulkanVideoRenderer: YCbCr image view creation failed");
        return nullptr;
    }

    if (usePushDesc_)
    {
        // No set: the view is pushed into the command buffer at draw time.
        auto pushed = frameTextures_.emplace(image, ft);
        return &pushed.first->second;
    }

    VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dai.descriptorPool = ycbcrDescPool_;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts = &ycbcrSetLayout_;
    if (vkAllocateDescriptorSets(device_, &dai, &ft.set) != VK_SUCCESS)
    {
        vkDestroyImageView(device_, ft.view, nullptr);
        Log::Error("VulkanVideoRenderer: YCbCr descriptor set allocation failed");
        return nullptr;
    }

    VkDescriptorImageInfo dii{};
    dii.sampler = ycbcrSampler_; // ignored (immutable in the layout) but set for clarity
    dii.imageView = ft.view;
    dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet w0{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w0.dstSet = ft.set;
    w0.dstBinding = 0;
    w0.descriptorCount = 1;
    w0.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w0.pImageInfo = &dii;
    vkUpdateDescriptorSets(device_, 1, &w0, 0, nullptr);

    auto res = frameTextures_.emplace(image, ft);
    return &res.first->second;
}

bool VulkanVideoRenderer::RecordFrameTransition(uint64_t image, int oldLayout, uint32_t srcQueueFamily)
{
    VkCommandBuffer cmd = backend_->FrameOpsCmd();
    if (cmd == VK_NULL_HANDLE)
    {
        return false;
    }

    const uint32_t gfxFamily = backend_->GraphicsQueueFamily();
    const bool ownershipXfer = srcQueueFamily != VK_QUEUE_FAMILY_IGNORED && srcQueueFamily != gfxFamily;

    // Acquire half of the decode→graphics hand-off. The timeline-semaphore wait on the
    // submit (stage: FRAGMENT_SHADER) already orders this against the decode write and
    // makes it visible, so the barrier's src scope chains off that same stage rather
    // than blocking the whole pipe.
    VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    b.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    b.srcAccessMask = VK_ACCESS_2_NONE;
    b.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    b.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    b.oldLayout = static_cast<VkImageLayout>(oldLayout);
    b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcQueueFamilyIndex = ownershipXfer ? srcQueueFamily : VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = ownershipXfer ? gfxFamily : VK_QUEUE_FAMILY_IGNORED;
    b.image = reinterpret_cast<VkImage>(static_cast<uintptr_t>(image));
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &dep);
    return true;
}

bool VulkanVideoRenderer::DrawVulkanFrame(int fbX, int fbY, int fbW, int fbH)
{
    VulkanFrameInfo info{};
    if (!GetVulkanFrameInfo(vkFrame_, info) || !info.valid || info.image == 0)
    {
        vkFrame_ = nullptr; // not a usable Vulkan frame (e.g. multi-image fallback)
        return false;
    }
    // The decoder can rebuild its hw-frames pool without a format change (seeks, stream
    // switches); the cached views then dangle over destroyed images whose handle values
    // the driver may reuse. Drop the cache whenever the pool identity changes.
    if (info.framesContextId != framesContextId_)
    {
        InvalidateFrameTextures();
        framesContextId_ = info.framesContextId;
    }
    if (!EnsureYcbcr(info.vkFormat, info.colorSpace, info.colorRange))
    {
        return false;
    }
    const FrameTex* ft = EnsureFrameTexture(info.image);
    if (!ft)
    {
        return false;
    }

    // The decoder always hands us a fresh frame in its decode layout, owned by the decode queue;
    // only WE ever leave a frame in SHADER_READ_ONLY_OPTIMAL owned by the graphics queue (step 4
    // below). So observing that state means this is a re-present of a frame we already prepared —
    // the display is repainting an unchanged frame (paused, or video fps < display refresh). We
    // hold a ref on the AVVkFrame, so the decoder cannot reuse/overwrite the image meanwhile, and
    // same-queue ordering already serialises this sample after the previous one — so there is
    // nothing to wait on. Re-running the decode→sample wait every such present is what pegged the
    // graphics queue: each present's TOP_OF_PIPE wait self-chains onto the previous present's
    // signal, which FIFO paces to vblank, so the queue sat blocked for most of every frame.
    const bool alreadyPrepared = info.layout == static_cast<int>(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) &&
                                 info.queueFamily == backend_->GraphicsQueueFamily();

    const auto frameSem = reinterpret_cast<VkSemaphore>(static_cast<uintptr_t>(info.semaphore));
    if (!alreadyPrepared)
    {
        // 1. Record the decode→sample layout transition (+ queue-ownership acquire) into
        //    the frame-ops command buffer (runs before Qt's scene-graph submit) and queue
        //    the decode semaphore as a wait on that same batched submit.
        if (!RecordFrameTransition(info.image, info.layout, info.queueFamily))
        {
            Log::Error("VulkanVideoRenderer: zero-copy transition record failed");
            return false;
        }
        backend_->AddFrameOpsWait(frameSem, info.semValue, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
    }

    // 3. Draw the YCbCr image (conversion → RGB in the sampler), letterboxed within the
    //    caller's target rect (excludes the fallback title bar strip when present).
    VkCommandBuffer cmd = backend_->CurrentCommandBuffer();
    const int vw = info.width > 0 ? info.width : vkDisplayW_;
    const int vh = info.height > 0 ? info.height : vkDisplayH_;
    const LetterboxRect lb = ComputeLetterbox(fbX, fbY, fbW, fbH, vw, vh);

    VkViewport vp{};
    vp.x = static_cast<float>(lb.x);
    vp.y = static_cast<float>(lb.y);
    vp.width = static_cast<float>(lb.w);
    vp.height = static_cast<float>(lb.h);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    VkRect2D scissor{{lb.x, lb.y}, {static_cast<uint32_t>(lb.w), static_cast<uint32_t>(lb.h)}};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ycbcrPipeline_);
    if (usePushDesc_)
    {
        VkDescriptorImageInfo dii{};
        dii.imageView = ft->view; // sampler comes from the immutable-sampler layout
        dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet w0{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w0.dstBinding = 0;
        w0.descriptorCount = 1;
        w0.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w0.pImageInfo = &dii;
        backend_->CmdPushDescriptorSet(cmd, ycbcrPipelineLayout_, w0);
    }
    else
    {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ycbcrPipelineLayout_, 0, 1, &ft->set, 0, nullptr);
    }
    vkCmdDraw(cmd, 3, 1, 0, 0);

    // 4. Publish the post-sample state so FFmpeg/next consumer observes the right layout,
    //    timeline value and owning queue family. Only on a freshly prepared frame — a re-present
    //    neither transitioned the image nor advanced the timeline, so its state already stands.
    if (!alreadyPrepared)
    {
        backend_->QueueFrameSignal(frameSem, info.semValue + 1);
        SetVulkanFrameState(
            vkFrame_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, info.semValue + 1, backend_->GraphicsQueueFamily()
        );
    }
    return true;
}
