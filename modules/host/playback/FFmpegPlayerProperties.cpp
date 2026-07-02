#include "FFmpegPlayer.h"

#include "FFmpegPlayerInternal.h"

#include <cstddef>
#include <mutex>
#include <string>

// ── Async property queries ────────────────────────────────────────────────────

void FFmpegPlayer::GetDoubleAsync(PlayerProperty prop, void (*cb)(double, bool, void*), void* ud) noexcept
{
    if (!cb)
    {
        return;
    }
    const bool playing = !idle_.load();
    switch (prop)
    {
    case PlayerProperty::TimePos:
        cb(GetMasterClock(), playing, ud);
        return;
    case PlayerProperty::Duration: {
        const double d = duration_.load();
        cb(d, d > 0.0, ud);
        return;
    }
    case PlayerProperty::PercentPos: {
        const double d = duration_.load();
        cb(d > 0.0 ? GetMasterClock() / d * 100.0 : 0.0, playing && d > 0.0, ud);
        return;
    }
    case PlayerProperty::Volume:
        cb(static_cast<double>(volume_), true, ud);
        return;
    case PlayerProperty::Speed:
        cb(speed_, true, ud);
        return;
    default:
        cb(0.0, false, ud);
        return;
    }
}

void FFmpegPlayer::GetInt64Async(PlayerProperty prop, void (*cb)(int64_t, bool, void*), void* ud) noexcept
{
    if (!cb)
    {
        return;
    }
    switch (prop)
    {
    case PlayerProperty::DisplayWidth: {
        const int64_t v = displayWidth_.load();
        cb(v, v > 0, ud);
        return;
    }
    case PlayerProperty::DisplayHeight: {
        const int64_t v = displayHeight_.load();
        cb(v, v > 0, ud);
        return;
    }
    case PlayerProperty::DroppedFrames:
        cb(droppedFrames_.load(), true, ud);
        return;
    case PlayerProperty::MistimedFrames:
        cb(mistimedFrames_.load(), true, ud);
        return;
    case PlayerProperty::DecodeErrors:
        cb(decodeErrors_.load(), true, ud);
        return;
    case PlayerProperty::CacheUsed:
        cb(cache_.UsedKB(), true, ud);
        return;
    case PlayerProperty::CacheHits:
        cb(cache_.Hits(), true, ud);
        return;
    case PlayerProperty::CacheMisses:
        cb(cache_.Misses(), true, ud);
        return;
    default:
        cb(0, false, ud);
        return;
    }
}

void FFmpegPlayer::GetStringAsync(PlayerProperty prop, void (*cb)(const char*, bool, void*), void* ud) noexcept
{
    if (!cb)
    {
        return;
    }
    if (prop == PlayerProperty::Path)
    {
        std::string p;
        {
            std::lock_guard lock(mutex_);
            p = currentPath_;
        }
        cb(p.c_str(), !p.empty(), ud);
        return;
    }
    if (prop == PlayerProperty::MediaTitle)
    {
        std::string t;
        {
            std::lock_guard lock(mutex_);
            t = mediaTitle_;
        }
        cb(t.c_str(), !t.empty(), ud);
        return;
    }
    if (prop == PlayerProperty::HwDecCurrent)
    {
        std::string name;
        {
            std::lock_guard lock(mutex_);
            name = hwDecName_;
        }
        cb(name.c_str(), true, ud);
        return;
    }
    cb("", false, ud);
}

void FFmpegPlayer::GetDisplaySizeAsync(void (*cb)(const DisplaySize*, bool, void*), void* ud) noexcept
{
    if (!cb)
    {
        return;
    }
    const int64_t w = displayWidth_.load();
    const int64_t h = displayHeight_.load();
    if (w > 0 && h > 0)
    {
        const DisplaySize size{w, h};
        cb(&size, true, ud);
    }
    else
    {
        cb(nullptr, false, ud);
    }
}

void FFmpegPlayer::ObserveProperty(PlayerProperty prop) noexcept
{
    if (static_cast<std::size_t>(prop) >= PlayerEventSink::kPropCount)
    {
        return;
    }
    eventSink_.Observe(prop);

    // Emit the current value on subscription (so subscribers get an initial snapshot).
    switch (prop)
    {
    case PlayerProperty::IdleActive:
        EmitFlag(prop, idle_.load());
        break;
    case PlayerProperty::Pause:
        EmitFlag(prop, paused_.load());
        break;
    case PlayerProperty::Mute:
        EmitFlag(prop, muteEnabled_);
        break;
    case PlayerProperty::Normalize:
        EmitFlag(prop, normalizeEnabled_.load());
        break;
    case PlayerProperty::CoreIdle:
        EmitFlag(prop, coreIdle_.load());
        break;
    case PlayerProperty::EofReached:
        EmitFlag(prop, eofReached_.load());
        break;
    case PlayerProperty::Seeking:
        EmitFlag(prop, false); // not seeking at subscription time
        break;
    case PlayerProperty::PausedForCache:
        EmitFlag(prop, cache_.Stalling()); // true while a decode worker is stalled on a cache underrun
        break;
    case PlayerProperty::Duration:
        EmitDouble(prop, duration_.load());
        break;
    case PlayerProperty::Volume:
        EmitDouble(prop, static_cast<double>(volume_));
        break;
    case PlayerProperty::Speed:
        EmitDouble(prop, speed_);
        break;
    default:
        break; // TimePos / PercentPos stream from the workers
    }
}
