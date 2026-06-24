#pragma once
#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

#include <framelift/services/ILogBuffer.h>

// Thread-safe, fixed-capacity ring buffer of recent log lines. Fed by the host
// log sink (Log.cpp), which is called from many threads (decode thread, A/V
// workers, main), and read back by the Log Viewer plugin via the ILogBuffer
// service. In-memory only — nothing is persisted to disk.
class LogBuffer final : public ILogBuffer
{
public:
    explicit LogBuffer(std::size_t capacity = 4096);

    // Append one formatted line. `level` is a Log::Level value cast to int.
    void Push(int level, const char* msg) noexcept;

    // ── ILogBuffer ───────────────────────────────────────────────────────────
    [[nodiscard]] unsigned long long LatestSeq() const noexcept override;
    unsigned long long ReadSince(unsigned long long afterSeq, Visitor visit, void* ud) const noexcept override;

private:
    struct Entry
    {
        unsigned long long seq = 0;
        long long tsMillis = 0;
        int level = 0;
        std::string msg;
    };

    mutable std::mutex mutex_;
    std::vector<Entry> ring_; // size() grows to capacity_, then wraps via head_
    std::size_t capacity_;
    std::size_t head_ = 0;          // index of the oldest entry once full
    unsigned long long nextSeq_ = 1; // first published entry gets seq 1
};

// Process-wide buffer. Defined as a function-local static so the free-function
// log sink can reach it without being plumbed through App construction.
LogBuffer& HostLogBuffer();
