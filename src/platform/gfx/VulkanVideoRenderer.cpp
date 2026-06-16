#include "VulkanVideoRenderer.h"

#include "VulkanGraphicsBackend.h"

// Stub implementation. Real upload + pipeline + letterbox draw land in the next step;
// for now Draw is a no-op so the backend's render-pass clear shows black behind the UI.

VulkanVideoRenderer::VulkanVideoRenderer(VulkanGraphicsBackend* backend) : backend_(backend)
{
}

VulkanVideoRenderer::~VulkanVideoRenderer() = default;

bool VulkanVideoRenderer::Init(IGraphicsBackend* /*backend*/)
{
    return backend_ != nullptr;
}

void VulkanVideoRenderer::Upload(const uint8_t* /*rgba*/, int /*w*/, int /*h*/)
{
}

void VulkanVideoRenderer::UploadOverlay(const uint8_t* /*rgba*/, int /*w*/, int /*h*/)
{
}

void VulkanVideoRenderer::Draw(int /*fbW*/, int /*fbH*/, bool /*drawOverlay*/)
{
}
