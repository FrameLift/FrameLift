#pragma once

#include <cstdint>

// FFmpeg AVColorSpace / AVColorRange → Vulkan sampler-YCbCr conversion constants.
// Vulkan-free on purpose (the VulkanQueueLock.h precedent) so the mapping table is
// unit-testable; the returned values mirror the VkSamplerYcbcrModelConversion /
// VkSamplerYcbcrRange enums, which VulkanVideoRenderer static_asserts against the
// real definitions before casting.
namespace VulkanColorMapping
{
inline constexpr int32_t kModelYcbcr709 = 2;  // VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_709
inline constexpr int32_t kModelYcbcr601 = 3;  // VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_601
inline constexpr int32_t kModelYcbcr2020 = 4; // VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_2020
inline constexpr int32_t kRangeItuFull = 0;   // VK_SAMPLER_YCBCR_RANGE_ITU_FULL
inline constexpr int32_t kRangeItuNarrow = 1; // VK_SAMPLER_YCBCR_RANGE_ITU_NARROW

// AVColorSpace uses ISO/ITU numeric values (stable FFmpeg ABI).
constexpr int32_t ModelFromAvColorSpace(int avColorSpace)
{
    switch (avColorSpace)
    {
    case 1: // AVCOL_SPC_BT709
        return kModelYcbcr709;
    case 4: // AVCOL_SPC_FCC
    case 5: // AVCOL_SPC_BT470BG (BT.601 625)
    case 6: // AVCOL_SPC_SMPTE170M (BT.601 525)
    case 7: // AVCOL_SPC_SMPTE240M
        return kModelYcbcr601;
    case 9:  // AVCOL_SPC_BT2020_NCL
    case 10: // AVCOL_SPC_BT2020_CL
        return kModelYcbcr2020;
    default:
        return kModelYcbcr709; // sensible HD default
    }
}

// AVColorRange: 2 == AVCOL_RANGE_JPEG (full); anything else treated as limited/narrow.
constexpr int32_t RangeFromAvColorRange(int avColorRange)
{
    return avColorRange == 2 ? kRangeItuFull : kRangeItuNarrow;
}
} // namespace VulkanColorMapping
