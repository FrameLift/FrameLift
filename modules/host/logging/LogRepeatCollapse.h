#pragma once

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>

// Collapses repeat storms of identical log lines: the first occurrence emits
// immediately, identical follow-ups within the window are counted, and the tally is
// folded into one "(xN)" line — either when the same message recurs after the window,
// or when a different message arrives (flushed first, so no occurrence is ever
// silently dropped). Some libav decode paths emit the same line once per packet
// (e.g. mp3 "Header missing" for every misaligned packet after each seek in an AVI),
// which otherwise turns a held seek key into a screenful.
//
// Deliberately dependency-free (chrono + libc only) so the platform-independent test
// suite covers it; the caller owns locking and the mapping of levels to Log::*.
class LogRepeatCollapser
{
public:
    using Clock = std::chrono::steady_clock;

    explicit LogRepeatCollapser(Clock::duration window) : window_(window)
    {
    }

    // Observe one message. Invokes emit(level, message, occurrences) for each line to
    // log, in order: occurrences == 1 is a plain line, > 1 folds that many identical
    // occurrences into one line. Messages longer than the internal buffer are compared
    // on their truncated prefix (matching the fixed-size buffers of av_log callbacks).
    template <class EmitFn>
    void Observe(int level, const char* message, Clock::time_point now, EmitFn&& emit)
    {
        if (std::strncmp(message, lastMsg_, sizeof(lastMsg_) - 1) == 0)
        {
            if (now - lastEmit_ < window_)
            {
                ++suppressed_;
                return;
            }
            emit(level, message, suppressed_ + 1);
            suppressed_ = 0;
            lastLevel_ = level;
            lastEmit_ = now;
            return;
        }
        if (suppressed_ > 0)
        {
            // A different message while repeats were pending: surface the tally for
            // the previous message before switching to the new one.
            emit(lastLevel_, lastMsg_, suppressed_);
            suppressed_ = 0;
        }
        std::snprintf(lastMsg_, sizeof(lastMsg_), "%s", message);
        lastLevel_ = level;
        lastEmit_ = now;
        emit(level, message, std::uint64_t{1});
    }

private:
    Clock::duration window_;
    char lastMsg_[1024] = {};
    int lastLevel_ = 0;
    std::uint64_t suppressed_ = 0;
    Clock::time_point lastEmit_{};
};
