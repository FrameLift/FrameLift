#pragma once

#include <cstdint>
#include <vector>

// Physical-device selection policy, factored out of VulkanGraphicsBackend::CreateDevice
// as pure logic over pre-resolved capability flags. Vulkan-free on purpose (the
// VulkanQueueLock.h precedent) so the ranking is unit-testable; the backend fills one
// candidate per enumerated VkPhysicalDevice and indexes its Vulkan-side data with the
// returned index.
namespace VulkanDeviceSelect
{
// VK_API_VERSION_1_3 without the Vulkan header (variant 0, major 1, minor 3, patch 0);
// the backend static_asserts this equals the real macro.
inline constexpr uint32_t kMinApiVersion = (1u << 22) | (3u << 12);

struct Candidate
{
    uint32_t apiVersion = 0;
    bool hasGraphicsPresentQueue = false; // a queue family with GRAPHICS + present support
    bool hasSwapchainExtension = false;
    bool discrete = false;     // VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU
    bool videoCapable = false; // decode queue + exts + zero-copy feature prerequisites
};

constexpr bool Eligible(const Candidate& c)
{
    return c.apiVersion >= kMinApiVersion && c.hasGraphicsPresentQueue && c.hasSwapchainExtension;
}

// Best eligible candidate or -1: video-decode capability wins, then discrete GPUs,
// then enumeration order (drivers list the primary adapter first).
inline int SelectDevice(const std::vector<Candidate>& candidates)
{
    int best = -1;
    for (int i = 0; i < static_cast<int>(candidates.size()); ++i)
    {
        const Candidate& c = candidates[static_cast<size_t>(i)];
        if (!Eligible(c))
        {
            continue;
        }
        if (best < 0)
        {
            best = i;
            continue;
        }
        const Candidate& b = candidates[static_cast<size_t>(best)];
        if ((c.videoCapable && !b.videoCapable) || (c.videoCapable == b.videoCapable && c.discrete && !b.discrete))
        {
            best = i;
        }
    }
    return best;
}
} // namespace VulkanDeviceSelect
