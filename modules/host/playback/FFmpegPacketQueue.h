#pragma once

extern "C"
{
#include <libavcodec/packet.h>
}

#include "ReadAheadCache.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <queue>

// Thread-safe, bounded hand-off of demuxed packets to one persistent decode
// worker. A seek interrupts the current generation without terminating the
// worker: queued packets are discarded, a later BeginGeneration() delivers one
// Flush result before any packet from the new position, and the worker flushes
// its own codec context. Stop() is the only terminal result.
class FFmpegPacketQueue
{
public:
    enum class PopKind : std::uint8_t
    {
        Packet,
        Flush,
        Eof,
        Stop,
    };

    struct PopResult
    {
        PopKind kind = PopKind::Stop;
        std::uint64_t generation = 0;
        std::chrono::steady_clock::time_point requestedAt{};
    };

    explicit FFmpegPacketQueue(std::size_t maxPackets = 256) : maxPackets_(maxPackets)
    {
    }

    void SetBudget(ReadAheadCache* budget)
    {
        budget_ = budget;
    }

    ~FFmpegPacketQueue()
    {
        ClearPackets();
    }

    FFmpegPacketQueue(const FFmpegPacketQueue&) = delete;
    FFmpegPacketQueue& operator=(const FFmpegPacketQueue&) = delete;

    // Start accepting one generation. The worker observes Flush before any
    // packet with this generation, even when the demuxer wins the scheduling race.
    void BeginGeneration(std::uint64_t generation, bool flushDecoder)
    {
        {
            std::lock_guard lock(m_);
            ClearPacketsLocked();
            generation_ = generation;
            interrupted_ = false;
            stopped_ = false;
            flushPending_ = flushDecoder;
            flushRequestedAt_ =
                flushDecoder ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
            eof_ = false;
            eofDelivered_ = false;
            eofAcknowledged_ = false;
            primed_ = false;
        }
        notEmpty_.notify_all();
        notFull_.notify_all();
    }

    // Immediately invalidate the current generation and wake blocked producers.
    // Consumers stay parked until BeginGeneration() or Stop() supplies a command.
    void Interrupt()
    {
        {
            std::lock_guard lock(m_);
            ClearPacketsLocked();
            interrupted_ = true;
            eof_ = false;
            eofDelivered_ = false;
            eofAcknowledged_ = false;
            primed_ = false;
        }
        notEmpty_.notify_all();
        notFull_.notify_all();
        if (budget_)
        {
            budget_->Abort();
        }
    }

    // Terminal worker shutdown (file close, track rebind, application stop).
    void Stop()
    {
        {
            std::lock_guard lock(m_);
            ClearPacketsLocked();
            interrupted_ = true;
            stopped_ = true;
            eof_ = false;
            eofDelivered_ = false;
            eofAcknowledged_ = false;
        }
        notEmpty_.notify_all();
        notFull_.notify_all();
        if (budget_)
        {
            budget_->Abort();
        }
    }

    // Move pkt into the queue for generation. Returns false if a seek/stop
    // invalidated that generation while admission was blocked.
    bool Push(AVPacket* pkt, std::uint64_t generation)
    {
        const int64_t sz = pkt->size;
        AVPacket* owned = av_packet_alloc();
        if (!owned)
        {
            return false;
        }

        std::uint64_t budgetEpoch = 0;
        if (budget_ && !budget_->Reserve(sz, &budgetEpoch))
        {
            av_packet_free(&owned);
            return false;
        }
        av_packet_move_ref(owned, pkt);

        std::unique_lock lock(m_);
        notFull_.wait(
            lock,
            [this, generation]
            {
                return stopped_ || interrupted_ || generation != generation_ || q_.size() < maxPackets_;
            }
        );
        if (stopped_ || interrupted_ || generation != generation_)
        {
            lock.unlock();
            av_packet_free(&owned);
            if (budget_)
            {
                budget_->RemoveBytes(sz, budgetEpoch);
            }
            return false;
        }
        q_.push({owned, generation, budgetEpoch});
        primed_ = true;
        lock.unlock();
        notEmpty_.notify_one();
        return true;
    }

    // Pop one packet or lifecycle command. EOF is delivered once per generation;
    // after it the worker waits for the next generation instead of exiting.
    PopResult Pop(AVPacket* out)
    {
        std::unique_lock lock(m_);
        for (;;)
        {
            const bool stall =
                budget_ && q_.empty() && !stopped_ && !interrupted_ && !flushPending_ && !eof_ && primed_;
            if (stall)
            {
                budget_->RecordMiss();
                lock.unlock();
                budget_->BeginStall();
                lock.lock();
            }
            notEmpty_.wait(
                lock,
                [this]
                {
                    return stopped_ || flushPending_ || !q_.empty() || (eof_ && !eofDelivered_);
                }
            );
            if (stall)
            {
                lock.unlock();
                budget_->EndStall();
                lock.lock();
            }

            if (stopped_)
            {
                return {PopKind::Stop, generation_};
            }
            if (flushPending_)
            {
                flushPending_ = false;
                return {PopKind::Flush, generation_, flushRequestedAt_};
            }
            if (!q_.empty())
            {
                Entry entry = q_.front();
                q_.pop();
                const int64_t sz = entry.packet->size;
                lock.unlock();
                notFull_.notify_one();
                if (budget_)
                {
                    budget_->RemoveBytes(sz, entry.budgetEpoch);
                    budget_->RecordHit();
                }
                av_packet_move_ref(out, entry.packet);
                av_packet_free(&entry.packet);
                return {PopKind::Packet, entry.generation};
            }
            if (eof_ && !eofDelivered_)
            {
                eofDelivered_ = true;
                return {PopKind::Eof, generation_};
            }
        }
    }

    void SignalEof()
    {
        {
            std::lock_guard lock(m_);
            eof_ = true;
            eofDelivered_ = false;
            eofAcknowledged_ = false;
        }
        notEmpty_.notify_all();
    }

    void AcknowledgeEof(std::uint64_t generation)
    {
        {
            std::lock_guard lock(m_);
            if (generation == generation_ && eof_)
            {
                eofAcknowledged_ = true;
            }
        }
        notEmpty_.notify_all();
    }

    // The old per-seek worker join also acted as an EOF drain barrier. Persistent
    // workers acknowledge after decoder/filter drain; seek/stop cancels the wait.
    [[nodiscard]] bool WaitForEof(std::uint64_t generation)
    {
        std::unique_lock lock(m_);
        notEmpty_.wait(
            lock,
            [this, generation]
            {
                return stopped_ || interrupted_ || generation != generation_ || eofAcknowledged_;
            }
        );
        return eofAcknowledged_ && generation == generation_;
    }

    [[nodiscard]] bool Interrupted() const
    {
        std::lock_guard lock(m_);
        return interrupted_;
    }

    [[nodiscard]] bool AtEof() const
    {
        std::lock_guard lock(m_);
        return eof_;
    }

    [[nodiscard]] bool Stopped() const
    {
        std::lock_guard lock(m_);
        return stopped_;
    }

private:
    struct Entry
    {
        AVPacket* packet = nullptr;
        std::uint64_t generation = 0;
        std::uint64_t budgetEpoch = 0;
    };

    void ClearPackets()
    {
        std::lock_guard lock(m_);
        ClearPacketsLocked();
    }

    void ClearPacketsLocked()
    {
        while (!q_.empty())
        {
            Entry entry = q_.front();
            q_.pop();
            if (budget_)
            {
                budget_->RemoveBytes(entry.packet->size, entry.budgetEpoch);
            }
            av_packet_free(&entry.packet);
        }
    }

    mutable std::mutex m_;
    std::condition_variable notEmpty_;
    std::condition_variable notFull_;
    std::queue<Entry> q_;
    std::size_t maxPackets_;
    ReadAheadCache* budget_ = nullptr;
    std::uint64_t generation_ = 0;
    bool interrupted_ = true;
    bool stopped_ = false;
    bool flushPending_ = false;
    std::chrono::steady_clock::time_point flushRequestedAt_{};
    bool eof_ = false;
    bool eofDelivered_ = false;
    bool eofAcknowledged_ = false;
    bool primed_ = false;
};
