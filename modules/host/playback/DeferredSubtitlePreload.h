#pragma once

#include <algorithm>
#include <cstddef>

// Keep each mutex-held libass ingestion span short while deferred embedded
// subtitle preload hands its locally decoded cues to the active track.
namespace DeferredSubtitlePreload
{
inline constexpr std::size_t kCueBatchSize = 64;

[[nodiscard]] constexpr std::size_t CueBatchEnd(std::size_t begin, std::size_t total)
{
    return std::min(begin + kCueBatchSize, total);
}
} // namespace DeferredSubtitlePreload
