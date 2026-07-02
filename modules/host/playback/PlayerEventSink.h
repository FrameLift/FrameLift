#pragma once

#include <framelift/platform/IMediaPlayer.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <mutex>
#include <queue>

// MediaEvent queue + property-observation set behind the player's event surface:
// producers (decode thread / workers) Queue events, the main thread drains them
// via Poll after the wakeup callback fires, and Emit* gates PropertyChange
// events on what the host actually subscribed to with Observe.
//
// libav/Qt-free and internally locked. The lock is a leaf: nothing is called
// back while it is held (the wakeup callback fires outside it), so Queue/Emit*
// are safe from any thread, including under FFmpegPlayer::mutex_.
class PlayerEventSink
{
public:
    static constexpr std::size_t kPropCount = static_cast<std::size_t>(PlayerProperty::Unknown) + 1;

    void SetWakeupCallback(void (*fn)(void*), void* ud)
    {
        std::lock_guard lock(mutex_);
        wakeupCb_ = {fn, ud};
    }

    // Queue an event for the main thread and fire the wakeup callback (outside
    // the lock, so a callback that re-enters Poll/Queue can't deadlock).
    void Queue(const MediaEvent& e)
    {
        void (*fn)(void*) = nullptr;
        void* ud = nullptr;
        {
            std::lock_guard lock(mutex_);
            events_.push(e);
            fn = wakeupCb_.fn;
            ud = wakeupCb_.ud;
        }
        if (fn)
        {
            fn(ud);
        }
    }

    // Dequeue the oldest event; type == MediaEventType::None when empty.
    [[nodiscard]] MediaEvent Poll()
    {
        std::lock_guard lock(mutex_);
        if (events_.empty())
        {
            return MediaEvent{};
        }
        const MediaEvent e = events_.front();
        events_.pop();
        return e;
    }

    // Mark prop as host-subscribed; out-of-range values are ignored.
    void Observe(PlayerProperty prop)
    {
        const auto idx = static_cast<std::size_t>(prop);
        if (idx >= kPropCount)
        {
            return;
        }
        observed_[idx] = true;
    }

    [[nodiscard]] bool IsObserved(PlayerProperty prop) const
    {
        const auto idx = static_cast<std::size_t>(prop);
        return idx < kPropCount && observed_[idx].load();
    }

    // Queue a PropertyChange iff prop is currently observed.
    void EmitFlag(PlayerProperty prop, bool value)
    {
        if (!IsObserved(prop))
        {
            return;
        }
        MediaEvent e;
        e.type = MediaEventType::PropertyChange;
        e.property.prop = prop;
        e.property.type = PropertyType::Flag;
        e.property.value.flag = value ? 1 : 0;
        Queue(e);
    }

    void EmitDouble(PlayerProperty prop, double value)
    {
        if (!IsObserved(prop))
        {
            return;
        }
        MediaEvent e;
        e.type = MediaEventType::PropertyChange;
        e.property.prop = prop;
        e.property.type = PropertyType::Double;
        e.property.value.dbl = value;
        Queue(e);
    }

private:
    struct Callback
    {
        void (*fn)(void*) = nullptr;
        void* ud = nullptr;
    };

    mutable std::mutex mutex_; // guards events_ + wakeupCb_; leaf lock, never held across callbacks
    std::queue<MediaEvent> events_;
    Callback wakeupCb_;
    std::array<std::atomic<bool>, kPropCount> observed_{};
};
