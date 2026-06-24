#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <utility>

// Shared, memory-bounded read-ahead budget + metrics for the demuxer packet
// queues. One instance per loaded file is shared by the audio/video/
// subtitle FFmpegPacketQueues: each queue accounts the bytes of the packets it
// buffers against this common budget, so the demuxer reads ahead only until the
// total cached size reaches the configured limit, then backs off.
//
// This type is deliberately FFmpeg-free (no AVPacket / libav includes) so the
// bounding and hit/miss logic can be unit-tested in framelift_tests without
// pulling in SDL/FFmpeg. The queues touch the libav types; this only sees bytes.
class ReadAheadCache
{
public:
    ReadAheadCache() = default;

    ReadAheadCache(const ReadAheadCache&) = delete;
    ReadAheadCache& operator=(const ReadAheadCache&) = delete;

    // Set the budget. enabled == false disables byte-bounding entirely
    // (WaitForSpace returns immediately) so the queues fall back to their own
    // packet-count cap. Safe to call between files.
    void Configure(bool enabled, int64_t maxBytes)
    {
        std::lock_guard lock(m_);
        enabled_ = enabled;
        maxBytes_ = maxBytes > 0 ? maxBytes : 0;
    }

    [[nodiscard]] bool Enabled() const
    {
        std::lock_guard lock(m_);
        return enabled_;
    }

    // Block until `bytes` more can be admitted without exceeding the budget, or
    // the cache is aborted. Returns false if aborted while waiting (the caller
    // should drop the packet and unwind). A single packet larger than the whole
    // budget is always admitted once the cache is empty so playback can progress.
    bool WaitForSpace(int64_t bytes)
    {
        std::unique_lock lock(m_);
        cv_.wait(lock, [&] { return aborted_ || CanAccept(bytes); });
        return !aborted_;
    }

    // Account bytes now buffered in a queue (call right after a successful push).
    void AddBytes(int64_t bytes)
    {
        std::lock_guard lock(m_);
        used_ += bytes;
    }

    // Release bytes as a queue hands a packet to its decode worker. Wakes a
    // demuxer parked in WaitForSpace.
    void RemoveBytes(int64_t bytes)
    {
        {
            std::lock_guard lock(m_);
            used_ -= bytes;
            if (used_ < 0)
            {
                used_ = 0;
            }
        }
        cv_.notify_all();
    }

    [[nodiscard]] int64_t UsedBytes() const
    {
        std::lock_guard lock(m_);
        return used_;
    }

    [[nodiscard]] int64_t UsedKB() const
    {
        return UsedBytes() / 1024;
    }

    // A "hit" is a decode worker dequeueing a packet that was already buffered;
    // a "miss" is the worker stalling on an empty (non-EOF) queue — a real
    // read-ahead underrun.
    void RecordHit()
    {
        hits_.fetch_add(1, std::memory_order_relaxed);
    }
    void RecordMiss()
    {
        misses_.fetch_add(1, std::memory_order_relaxed);
    }

    [[nodiscard]] int64_t Hits() const
    {
        return hits_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] int64_t Misses() const
    {
        return misses_.load(std::memory_order_relaxed);
    }

    // Stall tracking for the "paused-for-cache" state: a queue brackets an
    // underrun wait with BeginStall()/EndStall(). The callback fires only on the
    // aggregate 0↔1 transition (first worker to stall / last to recover) so the
    // player can push a single PausedForCache PropertyChange per transition.
    void SetStallCallback(std::function<void(bool)> cb)
    {
        stallCb_ = std::move(cb);
    }

    void BeginStall()
    {
        if (stalls_.fetch_add(1, std::memory_order_relaxed) == 0 && stallCb_)
        {
            stallCb_(true);
        }
    }

    void EndStall()
    {
        if (stalls_.fetch_sub(1, std::memory_order_relaxed) == 1 && stallCb_)
        {
            stallCb_(false);
        }
    }

    [[nodiscard]] bool Stalling() const
    {
        return stalls_.load(std::memory_order_relaxed) > 0;
    }

    // Wake every waiter; subsequent WaitForSpace return false immediately. Used
    // to release the demuxer when abandoning the current file or seeking.
    void Abort()
    {
        {
            std::lock_guard lock(m_);
            aborted_ = true;
        }
        cv_.notify_all();
    }

    // Clear the accounting and the abort flag so the cache can be reused for the
    // next read (e.g. after a seek). Hit/miss metrics are intentionally kept.
    void Reset()
    {
        {
            std::lock_guard lock(m_);
            used_ = 0;
            aborted_ = false;
        }
        cv_.notify_all();
    }

    // Clear hit/miss metrics (called when a new file is loaded).
    void ResetMetrics()
    {
        hits_.store(0, std::memory_order_relaxed);
        misses_.store(0, std::memory_order_relaxed);
    }

private:
    // Admit if bounding is off, the cache is empty (always make progress, even
    // for an oversized packet), or the addition still fits the budget.
    [[nodiscard]] bool CanAccept(int64_t bytes) const
    {
        return !enabled_ || used_ == 0 || used_ + bytes <= maxBytes_;
    }

    mutable std::mutex m_;
    std::condition_variable cv_;
    bool enabled_ = false;
    bool aborted_ = false;
    int64_t maxBytes_ = 0;
    int64_t used_ = 0;
    std::atomic<int64_t> hits_{0};
    std::atomic<int64_t> misses_{0};
    std::atomic<int> stalls_{0};
    std::function<void(bool)> stallCb_;
};
