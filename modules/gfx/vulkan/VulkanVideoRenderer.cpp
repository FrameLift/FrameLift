#include "VulkanVideoRenderer.h"

#include "FFmpegLetterbox.h"
#include "FFmpegVulkanDevice.h" // neutral bridge (no libav/volk types) — reads AVVkFrame

#include <vk_mem_alloc.h>

#include <framelift/Log.h>

#include <cstring>

// Embedded SPIR-V (generated at build time by cmake/FrameLiftShaders.cmake).
#include "video.frag.spv.h"
#include "video.vert.spv.h"

VulkanVideoRenderer::VulkanVideoRenderer(VulkanGraphicsBackend* backend) : backend_(backend)
{
}

VulkanVideoRenderer::~VulkanVideoRenderer()
{
    if (device_ == VK_NULL_HANDLE)
    {
        return;
    }
    // The host destroys the player (and this renderer) before the backend tears down
    // the device (see App member order / destructor), so the device is still live.
    vkDeviceWaitIdle(device_);

    DestroyYcbcr();
    if (transitionPool_ != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(device_, transitionPool_, nullptr);
    }
    DestroyTexture(video_);
    DestroyTexture(overlay_);
    if (staging_ != VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(allocator_, staging_, stagingAlloc_);
    }
    if (pipeline_ != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device_, pipeline_, nullptr);
    }
    if (pipelineLayout_ != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    }
    if (descPool_ != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(device_, descPool_, nullptr);
    }
    if (setLayout_ != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(device_, setLayout_, nullptr);
    }
    if (sampler_ != VK_NULL_HANDLE)
    {
        vkDestroySampler(device_, sampler_, nullptr);
    }
}

bool VulkanVideoRenderer::Init(IGraphicsBackend* /*backend*/)
{
    if (!backend_)
    {
        return false;
    }
    device_ = backend_->Device();
    physicalDevice_ = backend_->PhysicalDevice();
    allocator_ = backend_->Allocator();

    // Transient command buffers for the zero-copy frame layout/ownership transitions
    // (#18), which must run outside the swapchain render pass that Draw records into.
    VkCommandPoolCreateInfo tp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    tp.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    tp.queueFamilyIndex = backend_->GraphicsQueueFamily();
    if (vkCreateCommandPool(device_, &tp, nullptr, &transitionPool_) == VK_SUCCESS)
    {
        VkCommandBufferAllocateInfo ci{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ci.commandPool = transitionPool_;
        ci.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ci.commandBufferCount = static_cast<uint32_t>(transitionCmds_.size());
        vkAllocateCommandBuffers(device_, &ci, transitionCmds_.data());
    }

    // Sampler: linear, clamp-to-edge (matches GlVideoRenderer).
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = VK_FILTER_LINEAR;
    si.minFilter = VK_FILTER_LINEAR;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    if (vkCreateSampler(device_, &si, nullptr, &sampler_) != VK_SUCCESS)
    {
        Log::Error("VulkanVideoRenderer: sampler creation failed");
        return false;
    }

    // Descriptor set layout: binding 0 = combined image sampler (fragment stage).
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount = 1;
    li.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(device_, &li, nullptr, &setLayout_) != VK_SUCCESS)
    {
        Log::Error("VulkanVideoRenderer: descriptor set layout creation failed");
        return false;
    }

    // One descriptor set each for the video and overlay textures.
    constexpr uint32_t kSets = 2;
    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kSets};
    VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pi.maxSets = kSets;
    pi.poolSizeCount = 1;
    pi.pPoolSizes = &ps;
    if (vkCreateDescriptorPool(device_, &pi, nullptr, &descPool_) != VK_SUCCESS)
    {
        Log::Error("VulkanVideoRenderer: descriptor pool creation failed");
        return false;
    }

    return BuildPipeline();
}

VkShaderModule VulkanVideoRenderer::CreateShaderModule(const uint32_t* code, size_t sizeBytes) const
{
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = sizeBytes;
    ci.pCode = code;
    VkShaderModule m = VK_NULL_HANDLE;
    vkCreateShaderModule(device_, &ci, nullptr, &m);
    return m;
}

bool VulkanVideoRenderer::BuildPipeline()
{
    return CreateBlitPipeline(setLayout_, pipelineLayout_, pipeline_);
}

bool VulkanVideoRenderer::CreateBlitPipeline(VkDescriptorSetLayout setLayout, VkPipelineLayout& outLayout,
                                             VkPipeline& outPipeline)
{
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &setLayout;
    if (vkCreatePipelineLayout(device_, &pl, nullptr, &outLayout) != VK_SUCCESS)
    {
        Log::Error("VulkanVideoRenderer: pipeline layout creation failed");
        return false;
    }

    VkShaderModule vs = CreateShaderModule(kVideoVertSpv, sizeof(kVideoVertSpv));
    VkShaderModule fs = CreateShaderModule(kVideoFragSpv, sizeof(kVideoFragSpv));
    if (vs == VK_NULL_HANDLE || fs == VK_NULL_HANDLE)
    {
        Log::Error("VulkanVideoRenderer: shader module creation failed");
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
    vp.scissorCount = 1; // both dynamic

    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Straight-alpha blending: opaque video (alpha=1) replaces; the overlay composites.
    VkPipelineColorBlendAttachmentState cba{};
    cba.blendEnable = VK_TRUE;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.colorBlendOp = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    cba.alphaBlendOp = VK_BLEND_OP_ADD;
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                         VK_COLOR_COMPONENT_A_BIT;
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
    gp.layout = outLayout;
    gp.renderPass = backend_->RenderPass();
    gp.subpass = 0;

    const VkResult r = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &outPipeline);
    vkDestroyShaderModule(device_, vs, nullptr);
    vkDestroyShaderModule(device_, fs, nullptr);
    if (r != VK_SUCCESS)
    {
        Log::Error("VulkanVideoRenderer: graphics pipeline creation failed");
        return false;
    }
    return true;
}

void VulkanVideoRenderer::DestroyTexture(Texture& t)
{
    if (t.view != VK_NULL_HANDLE)
    {
        vkDestroyImageView(device_, t.view, nullptr);
    }
    if (t.image != VK_NULL_HANDLE)
    {
        vmaDestroyImage(allocator_, t.image, t.alloc);
    }
    t = {};
}

bool VulkanVideoRenderer::EnsureTexture(Texture& t, int w, int h)
{
    if (t.valid && t.w == w && t.h == h)
    {
        return true;
    }

    // Reallocate at the new size; the descriptor set is re-pointed at the new view.
    // If the old image was ever used, an in-flight frame may still sample it — resolution
    // changes are rare, so a device-wait is the simplest safe guard against use-after-free.
    if (t.valid)
    {
        vkDeviceWaitIdle(device_);
    }
    const VkDescriptorSet keepSet = t.set; // sets are pool-allocated once (see below)
    DestroyTexture(t);
    t.set = keepSet;

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

    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_AUTO;
    if (vmaCreateImage(allocator_, &ici, &aci, &t.image, &t.alloc, nullptr) != VK_SUCCESS)
    {
        Log::Error("VulkanVideoRenderer: image allocation failed ({}x{})", w, h);
        return false;
    }

    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = t.image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = VK_FORMAT_R8G8B8A8_UNORM;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(device_, &vci, nullptr, &t.view) != VK_SUCCESS)
    {
        Log::Error("VulkanVideoRenderer: image view creation failed");
        return false;
    }

    // Allocate the descriptor set once (first use), then just re-point it.
    if (t.set == VK_NULL_HANDLE)
    {
        VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        dai.descriptorPool = descPool_;
        dai.descriptorSetCount = 1;
        dai.pSetLayouts = &setLayout_;
        if (vkAllocateDescriptorSets(device_, &dai, &t.set) != VK_SUCCESS)
        {
            Log::Error("VulkanVideoRenderer: descriptor set allocation failed");
            return false;
        }
    }

    VkDescriptorImageInfo dii{sampler_, t.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet w0{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w0.dstSet = t.set;
    w0.dstBinding = 0;
    w0.descriptorCount = 1;
    w0.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w0.pImageInfo = &dii;
    vkUpdateDescriptorSets(device_, 1, &w0, 0, nullptr);

    t.w = w;
    t.h = h;
    t.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    t.valid = true;
    return true;
}

bool VulkanVideoRenderer::EnsureStaging(VkDeviceSize bytes)
{
    if (staging_ != VK_NULL_HANDLE && stagingSize_ >= bytes)
    {
        return true;
    }
    if (staging_ != VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(allocator_, staging_, stagingAlloc_);
        staging_ = VK_NULL_HANDLE;
        stagingMapped_ = nullptr;
    }

    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = bytes;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_AUTO;
    aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo info{};
    if (vmaCreateBuffer(allocator_, &bi, &aci, &staging_, &stagingAlloc_, &info) != VK_SUCCESS)
    {
        Log::Error("VulkanVideoRenderer: staging buffer allocation failed");
        return false;
    }
    stagingMapped_ = info.pMappedData;
    stagingSize_ = bytes;
    return true;
}

namespace
{
// Records the staging->image copy for ImmediateSubmit (transition, copy, transition).
struct CopyJob
{
    VkBuffer staging;
    VkImage image;
    VkImageLayout oldLayout;
    uint32_t w, h;
};

void RecordCopy(VkCommandBuffer cmd, void* ud)
{
    const CopyJob& j = *static_cast<CopyJob*>(ud);

    // When reusing the image, the src scope must cover prior frames' fragment-shader
    // reads on this (graphics) queue, so the transfer doesn't overwrite a frame still
    // being sampled. (submission order on the queue makes that dependency reach back to
    // earlier-submitted render work.) Fresh images start UNDEFINED — nothing to wait on.
    const bool reuse = j.oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkImageMemoryBarrier toDst{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toDst.oldLayout = j.oldLayout;
    toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.image = j.image;
    toDst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    toDst.srcAccessMask = reuse ? VK_ACCESS_SHADER_READ_BIT : 0;
    toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, reuse ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toDst);

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

void VulkanVideoRenderer::UploadTo(Texture& t, const uint8_t* rgba, int w, int h)
{
    if (!rgba || w <= 0 || h <= 0)
    {
        return;
    }
    if (!EnsureTexture(t, w, h))
    {
        return;
    }
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(w) * h * 4;
    if (!EnsureStaging(bytes) || !stagingMapped_)
    {
        return;
    }
    std::memcpy(stagingMapped_, rgba, static_cast<size_t>(bytes));

    CopyJob job{staging_, t.image, t.layout, static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
    backend_->ImmediateSubmit(RecordCopy, &job);
    t.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

void VulkanVideoRenderer::Upload(const uint8_t* rgba, int w, int h)
{
    vkFrame_ = nullptr; // switched (back) to the CPU-RGBA8 path
    UploadTo(video_, rgba, w, h);
}

void VulkanVideoRenderer::UploadVulkanFrame(void* avFrame, int displayW, int displayH)
{
    // Just stash the frame; the actual transition/sample happens in Draw (the timeline
    // wait/signal must join the per-frame submit). Called every render frame, even when
    // the frame is unchanged (paused), so the image is re-sampled with a fresh value.
    vkFrame_ = avFrame;
    vkDisplayW_ = displayW;
    vkDisplayH_ = displayH;
}

void VulkanVideoRenderer::UploadOverlay(const uint8_t* rgba, int w, int h)
{
    UploadTo(overlay_, rgba, w, h);
}

namespace
{
// Map FFmpeg's AVColorSpace (ISO/ITU numeric values, stable ABI) to a Vulkan YCbCr model.
VkSamplerYcbcrModelConversion YcbcrModel(int avColorSpace)
{
    switch (avColorSpace)
    {
    case 1: // AVCOL_SPC_BT709
        return VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_709;
    case 4: // AVCOL_SPC_FCC
    case 5: // AVCOL_SPC_BT470BG (BT.601 625)
    case 6: // AVCOL_SPC_SMPTE170M (BT.601 525)
    case 7: // AVCOL_SPC_SMPTE240M
        return VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_601;
    case 9:  // AVCOL_SPC_BT2020_NCL
    case 10: // AVCOL_SPC_BT2020_CL
        return VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_2020;
    default:
        return VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_709; // sensible HD default
    }
}

// AVColorRange: 2 == AVCOL_RANGE_JPEG (full); anything else treated as limited/narrow.
VkSamplerYcbcrRange YcbcrRange(int avColorRange)
{
    return avColorRange == 2 ? VK_SAMPLER_YCBCR_RANGE_ITU_FULL : VK_SAMPLER_YCBCR_RANGE_ITU_NARROW;
}
} // namespace

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

bool VulkanVideoRenderer::EnsureYcbcr(int vkFormat, int colorSpace, int colorRange)
{
    if (ycbcrConversion_ != VK_NULL_HANDLE && ycbcrFormat_ == vkFormat && ycbcrColorSpace_ == colorSpace &&
        ycbcrColorRange_ == colorRange)
    {
        return true; // already built for this format/colourimetry
    }

    // Rebuild from scratch; views/sets baked against the old conversion must go too.
    vkDeviceWaitIdle(device_);
    DestroyYcbcr();

    const auto fmt = static_cast<VkFormat>(vkFormat);

    // Pick chroma reconstruction settings the format actually supports.
    VkFormatProperties fp{};
    vkGetPhysicalDeviceFormatProperties(physicalDevice_, fmt, &fp);
    const VkFormatFeatureFlags feat = fp.optimalTilingFeatures;
    const VkFilter chromaFilter =
        (feat & VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_LINEAR_FILTER_BIT) ? VK_FILTER_LINEAR
                                                                                    : VK_FILTER_NEAREST;
    const VkChromaLocation xChroma = (feat & VK_FORMAT_FEATURE_COSITED_CHROMA_SAMPLES_BIT)
                                         ? VK_CHROMA_LOCATION_COSITED_EVEN
                                         : VK_CHROMA_LOCATION_MIDPOINT;

    VkSamplerYcbcrConversionCreateInfo cci{VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO};
    cci.format = fmt;
    cci.ycbcrModel = YcbcrModel(colorSpace);
    cci.ycbcrRange = YcbcrRange(colorRange);
    cci.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                      VK_COMPONENT_SWIZZLE_IDENTITY};
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
    li.bindingCount = 1;
    li.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(device_, &li, nullptr, &ycbcrSetLayout_) != VK_SUCCESS)
    {
        Log::Error("VulkanVideoRenderer: YCbCr set layout creation failed");
        return false;
    }

    // One descriptor set per pooled decode image (decoder reuses a small bounded set).
    constexpr uint32_t kMaxFrameSets = 16;
    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxFrameSets};
    VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pi.maxSets = kMaxFrameSets;
    pi.poolSizeCount = 1;
    pi.pPoolSizes = &ps;
    if (vkCreateDescriptorPool(device_, &pi, nullptr, &ycbcrDescPool_) != VK_SUCCESS)
    {
        Log::Error("VulkanVideoRenderer: YCbCr descriptor pool creation failed");
        return false;
    }

    if (!CreateBlitPipeline(ycbcrSetLayout_, ycbcrPipelineLayout_, ycbcrPipeline_))
    {
        return false;
    }

    ycbcrFormat_ = vkFormat;
    ycbcrColorSpace_ = colorSpace;
    ycbcrColorRange_ = colorRange;
    return true;
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
        vkDeviceWaitIdle(device_);
        for (auto& kv : frameTextures_)
        {
            vkDestroyImageView(device_, kv.second.view, nullptr);
        }
        frameTextures_.clear();
        vkResetDescriptorPool(device_, ycbcrDescPool_, 0);
    }

    FrameTex ft{};

    VkSamplerYcbcrConversionInfo convInfo{VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO};
    convInfo.conversion = ycbcrConversion_;
    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.pNext = &convInfo;
    vci.image = reinterpret_cast<VkImage>(static_cast<uintptr_t>(image));
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = static_cast<VkFormat>(ycbcrFormat_);
    vci.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                      VK_COMPONENT_SWIZZLE_IDENTITY};
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(device_, &vci, nullptr, &ft.view) != VK_SUCCESS)
    {
        Log::Error("VulkanVideoRenderer: YCbCr image view creation failed");
        return nullptr;
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

void VulkanVideoRenderer::RecordFrameTransition(uint64_t image, int oldLayout, uint32_t srcQueueFamily)
{
    const uint32_t slot = backend_->CurrentFrameIndex();
    VkCommandBuffer cmd = transitionCmds_[slot];
    if (cmd == VK_NULL_HANDLE)
    {
        return;
    }
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);

    const uint32_t gfxFamily = backend_->GraphicsQueueFamily();
    const bool ownershipXfer = srcQueueFamily != VK_QUEUE_FAMILY_IGNORED && srcQueueFamily != gfxFamily;

    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = static_cast<VkImageLayout>(oldLayout);
    b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcQueueFamilyIndex = ownershipXfer ? srcQueueFamily : VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = ownershipXfer ? gfxFamily : VK_QUEUE_FAMILY_IGNORED;
    b.image = reinterpret_cast<VkImage>(static_cast<uintptr_t>(image));
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.srcAccessMask = 0;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &b);
    vkEndCommandBuffer(cmd);

    // Run this transition in the single per-frame submit, just before the main render CB.
    // The decode-complete wait + post-sample signal are registered on that submit by the
    // caller (DrawVulkanFrame) via AddFrameWait/AddFrameSignal; in-batch CB ordering + the
    // barrier above guarantee the transition completes before the render pass samples.
    backend_->AddFramePreCmd(cmd);
}

void VulkanVideoRenderer::DrawVulkanFrame()
{
    VulkanFrameInfo info{};
    if (!GetVulkanFrameInfo(vkFrame_, info) || !info.valid || info.image == 0)
    {
        vkFrame_ = nullptr; // not a usable Vulkan frame (e.g. multi-image fallback)
        return;
    }
    if (!EnsureYcbcr(info.vkFormat, info.colorSpace, info.colorRange))
    {
        return;
    }
    const FrameTex* ft = EnsureFrameTexture(info.image);
    if (!ft)
    {
        return;
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
        // 1. Record the decode→sample layout transition (+ queue-ownership acquire) into a
        //    pre-cmd that runs first in the single per-frame submit, outside the render pass.
        RecordFrameTransition(info.image, info.layout, info.queueFamily);

        // 2. Wait until decode signalled `v` (gating the transition at TOP_OF_PIPE). Only needed
        //    on the first present of a freshly decoded frame; re-presents skip it (see above).
        backend_->AddFrameWait(frameSem, info.semValue, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    }

    // Signal `v+1` when this sample completes — the value FFmpeg/the next consumer waits on before
    // reusing the image. Emitted on EVERY present (the value advances each time via step 4), so the
    // guarantee covers the *last* sample of the frame even when re-presents skipped the wait.
    backend_->AddFrameSignal(frameSem, info.semValue + 1);

    // 3. Draw the YCbCr image (conversion → RGB in the sampler), letterboxed.
    VkCommandBuffer cmd = backend_->CurrentCommandBuffer();
    const VkExtent2D extent = backend_->SwapchainExtent();
    const int vw = info.width > 0 ? info.width : vkDisplayW_;
    const int vh = info.height > 0 ? info.height : vkDisplayH_;
    const LetterboxRect lb = ComputeLetterbox(static_cast<int>(extent.width), static_cast<int>(extent.height), vw, vh);

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
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ycbcrPipelineLayout_, 0, 1, &ft->set, 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);

    // 4. Publish the post-sample state so FFmpeg/next consumer observes the right layout,
    //    timeline value and owning queue family. Only on a freshly prepared frame — a re-present
    //    neither transitioned the image nor advanced the timeline, so its state already stands.
    if (!alreadyPrepared)
    {
        SetVulkanFrameState(vkFrame_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, info.semValue + 1,
                            backend_->GraphicsQueueFamily());
    }
}

void VulkanVideoRenderer::Draw(int /*fbW*/, int /*fbH*/, bool drawOverlay)
{
    VkCommandBuffer cmd = backend_->CurrentCommandBuffer();

    if (vkFrame_)
    {
        DrawVulkanFrame(); // zero-copy YCbCr video
    }
    else
    {
        const Texture& vid = video_;
        if (!vid.valid)
        {
            return; // backend's render-pass clear already painted black
        }

        const VkExtent2D extent = backend_->SwapchainExtent();
        // Aspect-preserving fit, centered. The overlay is uploaded at this same on-screen
        // size, so it maps 1:1 over the video within the same letterbox rectangle.
        const LetterboxRect lb =
            ComputeLetterbox(static_cast<int>(extent.width), static_cast<int>(extent.height), vid.w, vid.h);

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

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &vid.set, 0, nullptr);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }

    // Subtitle overlay (always RGBA via the standard pipeline), composited over the video
    // within the same letterbox viewport/scissor set above.
    if (drawOverlay && overlay_.valid)
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &overlay_.set, 0, nullptr);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }
}
