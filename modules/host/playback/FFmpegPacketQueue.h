#pragma once

extern "C"
{
#include <libavcodec/packet.h>
}

#include "ReadAheadCache.h"

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>

// Thread-safe, bounded hand-off of demuxed AVPackets from the demux thread to a
// single decode worker. One instance per stream (audio /
// video). The per-queue packet-count bound caps memory when one stream is read
// far ahead of the other: Push() blocks once the queue is full so the demuxer
// naturally paces itself.
//
// When a shared ReadAheadCache budget is attached, Push() reserves byte-space in
// the common budget, so read-ahead is bounded by a configured memory size across
// all streams; Pop() records hit/miss metrics and brackets
// underruns so the player can surface CacheUsed / PausedForCache.
//
// Header-only and ffmpeg-side (it touches AVPacket), so it is only ever compiled
// into the FFmpeg backend translation units, never the tests.
class FFmpegPacketQueue
{
public:
    explicit FFmpegPacketQueue(std::size_t maxPackets = 256) : maxPackets_(maxPackets)
    {
    }

    // Attach the shared read-ahead budget (null ⇒ packet-count bound only). Set
    // once before the queue is used by the demux/worker threads.
    void SetBudget(ReadAheadCache* budget)
    {
        budget_ = budget;
    }

    ~FFmpegPacketQueue()
    {
        Flush();
    }

    FFmpegPacketQueue(const FFmpegPacketQueue&) = delete;
    FFmpegPacketQueue& operator=(const FFmpegPacketQueue&) = delete;

    // Move pkt's contents into a queue-owned packet (pkt is left unreferenced and
    // reusable by the caller). Blocks while the queue is full. Returns false if
    // the queue was aborted while waiting.
    bool Push(AVPacket* pkt)
    {
        const int64_t sz = pkt->size;

        AVPacket* owned = av_packet_alloc();
        if (!owned)
        {
            return false;
        }

        // Reserve byte-space before taking ownership, so concurrent demuxers
        // cannot exceed the shared read-ahead budget between admission and
        // accounting.
        if (budget_ && !budget_->Reserve(sz))
        {
            av_packet_free(&owned);
            return false; // aborted while waiting (seek / stop)
        }
        av_packet_move_ref(owned, pkt);

        std::unique_lock lock(m_);
        notFull_.wait(lock, [this] { return abort_ || q_.size() < maxPackets_; });
        if (abort_)
        {
            lock.unlock();
            av_packet_free(&owned);
            if (budget_)
            {
                budget_->RemoveBytes(sz);
            }
            return false;
        }
        q_.push(owned);
        primed_ = true;
        lock.unlock();
        notEmpty_.notify_one();
        return true;
    }

    // Move the next packet into out (caller owns it and must av_packet_unref it).
    // Blocks until a packet is available, EOF is signalled, or the queue is
    // aborted. Returns false when the queue is drained at EOF or aborted — the
    // caller distinguishes via Aborted().
    bool Pop(AVPacket* out)
    {
        std::unique_lock lock(m_);

        // A non-EOF empty queue means the worker must wait for the demuxer to
        // catch up — a read-ahead underrun. Count it as a miss and bracket the
        // wait as a cache stall (drives PausedForCache); otherwise it's a hit.
        // Before the first Push after a Flush (primed_), an empty queue is the
        // expected pipeline fill after a seek/open — already inside the "seek"/
        // "file-open" spans — not an underrun, so it is neither a miss nor a stall.
        const bool stall = budget_ && q_.empty() && !eof_ && !abort_ && primed_;
        if (stall)
        {
            budget_->RecordMiss();
            // The cache callback reaches host code, so do not invoke it while
            // holding the packet-queue mutex.
            lock.unlock();
            budget_->BeginStall();
            lock.lock();
        }

        notEmpty_.wait(lock, [this] { return abort_ || eof_ || !q_.empty(); });

        if (stall)
        {
            lock.unlock();
            budget_->EndStall();
            lock.lock();
        }

        if (!q_.empty())
        {
            AVPacket* owned = q_.front();
            q_.pop();
            const int64_t sz = owned->size;
            lock.unlock();
            notFull_.notify_one();
            if (budget_)
            {
                budget_->RemoveBytes(sz);
                if (!stall)
                {
                    budget_->RecordHit();
                }
            }
            av_packet_move_ref(out, owned);
            av_packet_free(&owned);
            return true;
        }
        // Empty: either EOF (drained) or aborted.
        return false;
    }

    // No more packets will be pushed; Pop() drains the backlog then returns false.
    void SignalEof()
    {
        {
            std::lock_guard lock(m_);
            eof_ = true;
        }
        notEmpty_.notify_all();
    }

    // Wake every waiter; subsequent Push()/Pop() return false immediately. Used to
    // tear down a worker when abandoning the current file.
    void Abort()
    {
        {
            std::lock_guard lock(m_);
            abort_ = true;
        }
        notEmpty_.notify_all();
        notFull_.notify_all();
        // Release a demuxer parked in budget_->Reserve(). Queues are always
        // aborted as a group (seek / stop), so aborting the shared budget here is
        // safe; the player Reset()s it before resuming reads.
        if (budget_)
        {
            budget_->Abort();
        }
    }

    // Drop all queued packets and reset eof/abort so the queue can be reused for
    // the next file.
    void Flush()
    {
        std::lock_guard lock(m_);
        while (!q_.empty())
        {
            AVPacket* p = q_.front();
            q_.pop();
            if (budget_)
            {
                budget_->RemoveBytes(p->size);
            }
            av_packet_free(&p);
        }
        eof_ = false;
        abort_ = false;
        primed_ = false;
    }

    [[nodiscard]] bool Aborted() const
    {
        std::lock_guard lock(m_);
        return abort_;
    }

    [[nodiscard]] bool AtEof() const
    {
        std::lock_guard lock(m_);
        return eof_;
    }

private:
    mutable std::mutex m_;
    std::condition_variable notEmpty_;
    std::condition_variable notFull_;
    std::queue<AVPacket*> q_;
    std::size_t maxPackets_;
    ReadAheadCache* budget_ = nullptr; // shared byte budget + metrics; null ⇒ count bound only
    bool eof_ = false;
    bool abort_ = false;
    bool primed_ = false; // a Push has landed since the last Flush
};
