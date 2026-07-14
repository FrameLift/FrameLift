#pragma once

#include <cstdint>

namespace VulkanFrameBridge
{
struct Queue
{
    int family = -1;
    uint32_t index = 0;

    [[nodiscard]] bool Valid() const noexcept
    {
        return family >= 0;
    }
};

// Qt owns (graphicsFamily, qtGraphicsIndex). Timeline/binary conversion must use
// another VkQueue because Qt cannot participate in FrameLift's external mutex.
inline Queue SelectQueue(
    int graphicsFamily, uint32_t qtGraphicsIndex, int videoDecodeFamily, bool videoDecodeQueueAvailable
) noexcept
{
    if (graphicsFamily >= 0 && qtGraphicsIndex > 0)
    {
        return {graphicsFamily, 0};
    }
    if (videoDecodeQueueAvailable && videoDecodeFamily >= 0 && videoDecodeFamily != graphicsFamily)
    {
        return {videoDecodeFamily, 0};
    }
    return {};
}

inline bool GraphicsQueueIsIsolatedFromQt(uint32_t qtGraphicsIndex) noexcept
{
    return qtGraphicsIndex > 0;
}

enum class Phase : uint8_t
{
    Available,
    Collecting,
    QtSubmissionInstalled,
    CompletionInFlight,
    HostFallbackPending,
};

enum class ReuseAction : uint8_t
{
    None,
    WaitForCompletion,
    HostSignalCompletedQtWork,
    UnlockUnsubmitted,
};

inline bool TryMarkQtSubmissionInstalled(Phase& phase) noexcept
{
    if (phase != Phase::Collecting)
    {
        return false;
    }
    phase = Phase::QtSubmissionInstalled;
    return true;
}

inline ReuseAction ActionForReuse(Phase phase) noexcept
{
    switch (phase)
    {
    case Phase::CompletionInFlight:
        return ReuseAction::WaitForCompletion;
    case Phase::QtSubmissionInstalled:
    case Phase::HostFallbackPending:
        return ReuseAction::HostSignalCompletedQtWork;
    case Phase::Collecting:
        return ReuseAction::UnlockUnsubmitted;
    case Phase::Available:
        return ReuseAction::None;
    }
    return ReuseAction::None;
}
} // namespace VulkanFrameBridge
