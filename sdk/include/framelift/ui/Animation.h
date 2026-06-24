#pragma once
#include <chrono>
#include <framelift/ui/UIContext.h>

namespace framelift
{
// Wall-clock animation primitive for time-based UI effects (fades, slides, flashes),
// compiled into each plugin. Models a one-shot ramp: after an optional `delay`, Value()
// goes 0 → 1 over `duration` seconds. While the ramp is still running, sampling it calls
// UIContext::RequestRedraw() so the host keeps painting until it settles — then the
// event-driven render loop sleeps. This is how a component "declares" it is still animating;
// the author never touches steady_clock or the redraw flag directly.
//
// The common "hold fully on, then fade out" envelope used by transient overlays is
// `delay = hold`, `duration = fade`, with the displayed strength being `1 - Value(ctx)`:
//
//     if (!bar_.Active()) return;            // fully faded out — nothing to draw
//     const float alpha = 1.f - bar_.Value(ctx); // requests redraws while still fading
//
// All methods run on the render thread; no synchronization.
class Animation
{
public:
    // duration: ramp length in seconds (> 0). delay: seconds held before the ramp begins.
    // Starts in the finished/idle state — call Trigger() to run it.
    constexpr Animation(float duration, float delay = 0.0f) noexcept : duration_(duration), delay_(delay)
    {
    }

    // (Re)start the animation from now.
    void Trigger() noexcept
    {
        start_ = Clock::now();
        running_ = true;
    }

    // Jump to the finished/idle state (Active() == false).
    void Reset() noexcept
    {
        running_ = false;
    }

    // True while the ramp (including the initial delay) is still in progress. Does NOT
    // request a redraw, so it is safe to gate drawing on it (e.g. `if (!Active()) return;`).
    [[nodiscard]] bool Active() const noexcept
    {
        return running_ && Elapsed() < delay_ + duration_;
    }

    // Linear progress in [0, 1]: 0 throughout the delay, ramping to 1 across duration, then 1.
    // While the animation is still running this requests another frame from the host so it
    // keeps advancing; once finished it returns 1 and stops requesting (the loop can sleep).
    [[nodiscard]] float Value(UIContext& ctx) noexcept
    {
        if (!Active())
        {
            running_ = false;
            return 1.0f;
        }
        ctx.RequestRedraw();
        const float t = Elapsed() - delay_;
        if (t <= 0.0f)
        {
            return 0.0f;
        }
        const float v = t / duration_;
        return v < 1.0f ? v : 1.0f;
    }

private:
    using Clock = std::chrono::steady_clock;
    [[nodiscard]] float Elapsed() const noexcept
    {
        return std::chrono::duration<float>(Clock::now() - start_).count();
    }

    Clock::time_point start_{};
    float duration_;
    float delay_;
    bool running_ = false;
};
} // namespace framelift
