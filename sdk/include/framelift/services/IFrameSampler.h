#pragma once

// Host capability for reading decoded frame pixels out of a media file, off the
// playback path. IVideoOutput renders into the graphics surface and decoded frames
// never reach a plugin as CPU pixels; IFrameSampler opens its *own* demux + software
// decode session per file, seeks to a timestamp, and hands back a tightly packed RGBA
// image. Built for indexing/analysis (e.g. AI tagging) that samples a handful of
// frames per file independently of what is playing.
//
// Discover with ctx.GetService<IFrameSampler>() and null-check — capability discovery,
// not version negotiation. Adding this service does not bump FRAMELIFT_ABI_VERSION.
//
// Threading & lifetime: a session is an opaque handle returned by Open() and freed by
// Close(). ReadFrameRGBA() decodes synchronously and BLOCKS the calling thread — never
// call it from the UI thread; run it on your own worker. A single session is NOT
// thread-safe: serialize all calls that share one handle. Distinct sessions are
// independent and may run concurrently on different threads.
//
// v1 decodes in software only (predictable across hardware, no GPU contention with live
// playback). A hardware-decode path may be added later behind this same interface.
class IFrameSampler
{
public:
    static constexpr const char* InterfaceId = "framelift.IFrameSampler";
    virtual ~IFrameSampler() = default;

    // Open an independent demux + software-decode session for `path`. Returns an opaque
    // handle, or nullptr if the file cannot be opened or has no decodable video stream
    // (audio-only files and bare attached-picture streams are rejected). Free with Close.
    [[nodiscard]] virtual void* Open(const char* path) noexcept = 0;
    virtual void Close(void* session) noexcept = 0;

    // Total video duration in seconds, or 0.0 if unknown.
    [[nodiscard]] virtual double DurationSec(const void* session) const noexcept = 0;

    // Native (pre-scale) video dimensions. Writes into *w/*h (either may be null) and
    // returns false if the session is invalid.
    [[nodiscard]] virtual bool NativeSize(const void* session, int* w, int* h) const noexcept = 0;

    // Seek to `posSec`, decode the first frame at or after it, and scale + convert it to
    // tightly packed RGBA (row stride == outW*4). Pass outW==0 && outH==0 for native
    // size; otherwise the frame is scaled to exactly outW x outH. `buf` must hold at
    // least outW*outH*4 bytes (`cap`); the required size for native dimensions comes from
    // NativeSize. *actualSec (may be null) receives the decoded frame's presentation time.
    // Returns false on seek/decode/convert failure or an undersized buffer. A seek past
    // the end returns the final frame rather than failing.
    [[nodiscard]] virtual bool ReadFrameRGBA(
        void* session, double posSec, int outW, int outH, unsigned char* buf, int cap, double* actualSec
    ) noexcept = 0;
};
