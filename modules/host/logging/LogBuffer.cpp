#include "LogBuffer.h"

#include <chrono>

LogBuffer::LogBuffer(const std::size_t capacity) : capacity_(capacity == 0 ? 1 : capacity)
{
    ring_.reserve(capacity_);
}

void LogBuffer::Push(const int level, const char* msg) noexcept
{
    const auto nowMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();

    std::lock_guard lock(mutex_);
    Entry e{nextSeq_++, nowMs, level, msg ? msg : ""};

    if (ring_.size() < capacity_)
    {
        ring_.push_back(std::move(e));
    }
    else
    {
        ring_[head_] = std::move(e);
        head_ = (head_ + 1) % capacity_;
    }
}

unsigned long long LogBuffer::LatestSeq() const noexcept
{
    std::lock_guard lock(mutex_);
    return nextSeq_ - 1;
}

unsigned long long LogBuffer::ReadSince(const unsigned long long afterSeq, const Visitor visit, void* ud) const noexcept
{
    if (!visit)
    {
        return afterSeq;
    }

    std::lock_guard lock(mutex_);
    unsigned long long last = afterSeq;
    // Walk oldest → newest. When full, the oldest lives at head_; otherwise the
    // entries sit in insertion order from index 0.
    const std::size_t count = ring_.size();
    const bool full = count == capacity_;
    for (std::size_t i = 0; i < count; ++i)
    {
        const Entry& e = ring_[full ? (head_ + i) % capacity_ : i];
        if (e.seq <= afterSeq)
        {
            continue;
        }
        visit(ud, e.seq, e.tsMillis, e.level, e.msg.c_str());
        last = e.seq;
    }
    return last;
}

LogBuffer& HostLogBuffer()
{
    static LogBuffer instance;
    return instance;
}
