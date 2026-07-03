#pragma once

#include <cstdint>

// Sizing/alignment policy for the per-frame-slot staging arenas. Vulkan-free and pure
// (the VulkanQueueLock.h precedent) so the growth math is unit-testable.
namespace VulkanUploadPolicy
{
// Floor keeps steady-state 1080p RGBA video + overlay from ever growing twice.
inline constexpr uint64_t kMinStagingArena = 4ull << 20;

// Size for a replacement arena when `neededBytes` no longer fits: at least the need,
// at least double the outgoing buffer (amortized growth), never below the floor.
constexpr uint64_t NextStagingArenaSize(uint64_t currentSize, uint64_t neededBytes)
{
    const uint64_t doubled = currentSize * 2;
    uint64_t size = neededBytes > doubled ? neededBytes : doubled;
    return size > kMinStagingArena ? size : kMinStagingArena;
}

// Bump-allocator offsets stay 16-byte aligned — a multiple of every texel size used,
// as VkBufferImageCopy::bufferOffset requires.
constexpr uint64_t AlignArenaOffset(uint64_t offset)
{
    return (offset + 15) & ~uint64_t{15};
}
} // namespace VulkanUploadPolicy
