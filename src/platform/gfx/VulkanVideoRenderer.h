#pragma once

#include <cstdint>

#include "IVideoRenderer.h"

class VulkanGraphicsBackend;

// Vulkan blitter: uploads software-decoded RGBA frames to a sampled VkImage and draws
// them, letterboxed, into the active swapchain render pass the host also draws the UI
// into. Created by VulkanGraphicsBackend::CreateVideoRenderer(), so it holds the
// concrete backend for device/allocator/render-pass and the per-frame command buffer.
//
// NOTE: currently a stub (no-op uploads/draw) so the swapchain + ImGui plumbing can be
// validated with the UI rendering over a black video surface; the real pipeline lands
// in the next step.
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
    VulkanGraphicsBackend* backend_ = nullptr;
};
