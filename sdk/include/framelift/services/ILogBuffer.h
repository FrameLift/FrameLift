#pragma once

// In-memory log history exposed by the host. Logs normally flow one-way across
// the POD sink (see <framelift/Log.h>) into the host's logger; this service lets
// a plugin (e.g. a log viewer) read recent lines back out. Pull-based with a
// visitor callback so message lifetimes never escape the call. Adding this
// service does not bump FRAMELIFT_ABI_VERSION.
class ILogBuffer
{
public:
    static constexpr const char* InterfaceId = "framelift.ILogBuffer";
    virtual ~ILogBuffer() = default;

    // Invoked once per entry, in ascending sequence order. `msg` is a NUL-terminated,
    // call-scoped string (Log::Level cast to int for `level`); copy it out if retained.
    using Visitor = void (*)(void* ud, unsigned long long seq, long long tsMillis, int level, const char* msg);

    // Highest sequence number currently held (0 if the buffer is empty). Sequence
    // numbers are monotonic across the process; a consumer remembers the last value
    // it saw and passes it to ReadSince to fetch only what is new.
    [[nodiscard]] virtual unsigned long long LatestSeq() const noexcept = 0;

    // Visit every retained entry whose seq > afterSeq, oldest first. Returns the
    // highest seq visited (== afterSeq when nothing is new). Pass 0 to read all.
    virtual unsigned long long ReadSince(unsigned long long afterSeq, Visitor visit, void* ud) const noexcept = 0;
};
