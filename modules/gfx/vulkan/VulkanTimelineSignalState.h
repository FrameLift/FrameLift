#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <unordered_map>

// Pure bookkeeping for AVVkFrame timeline-semaphore values. FFmpeg and the renderer
// take turns signalling each frame semaphore, so values supplied by FFmpeg normally
// advance on their own. Keeping the last renderer reservation per pool makes stale or
// duplicated values harmless instead of emitting a spec-invalid signal.
class VulkanTimelineSignalState
{
public:
    struct Reservation
    {
        uint64_t value = 0;
        bool adjusted = false;
    };

    void BeginPool(uint64_t poolId)
    {
        if (poolId_ == poolId)
        {
            return;
        }
        poolId_ = poolId;
        values_.clear();
    }

    [[nodiscard]] uint64_t PoolId() const noexcept
    {
        return poolId_;
    }

    [[nodiscard]] std::optional<Reservation> Reserve(uint64_t semaphore, uint64_t requested)
    {
        if (semaphore == 0 || requested == 0)
        {
            return std::nullopt;
        }

        auto [it, inserted] = values_.try_emplace(semaphore, Entry{requested, 0});
        if (inserted || requested > it->second.reserved)
        {
            it->second.reserved = requested;
            return Reservation{requested, false};
        }
        if (it->second.reserved == std::numeric_limits<uint64_t>::max())
        {
            return std::nullopt;
        }

        ++it->second.reserved;
        return Reservation{it->second.reserved, true};
    }

    // Record that a reserved signal was actually delivered. Callers must not publish
    // AVVkFrame state until this succeeds.
    [[nodiscard]] bool MarkDelivered(uint64_t semaphore, uint64_t value)
    {
        const auto it = values_.find(semaphore);
        if (it == values_.end() || value > it->second.reserved || value <= it->second.delivered)
        {
            return false;
        }
        it->second.delivered = value;
        return true;
    }

    [[nodiscard]] uint64_t DeliveredValue(uint64_t semaphore) const noexcept
    {
        const auto it = values_.find(semaphore);
        return it == values_.end() ? 0 : it->second.delivered;
    }

private:
    struct Entry
    {
        uint64_t reserved = 0;
        uint64_t delivered = 0;
    };

    uint64_t poolId_ = 0;
    std::unordered_map<uint64_t, Entry> values_;
};
