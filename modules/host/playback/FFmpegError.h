#pragma once

#include <framelift/platform/IMediaPlayer.h>

// Pure AVERROR → EndFileReason classification for the FFmpeg backend (issue #13).
//
// Like FFmpegClock.h, this header is deliberately free of any libav include so it can
// be unit-tested with the native compiler (tests/unit/media/FFmpegErrorTests.cpp) — the
// standalone CI test build has no FFmpeg. It operates on the raw integer error codes
// FFmpeg returns. The few error constants it needs are mirrored here, computed exactly
// the way FFmpeg's MKTAG/FFERRTAG macros compute them; FFmpegPlayer.cpp (which does
// include FFmpeg) static_asserts each mirror against the real AVERROR_* value, so a
// drift in either direction is a compile error in the real build.

namespace ffmpeg_error_detail
{
// Mirror of libavutil's MKTAG(a,b,c,d) and FFERRTAG(a,b,c,d):
//   MKTAG    = a | (b<<8) | (c<<16) | ((unsigned)d<<24)
//   FFERRTAG = -(int)MKTAG(a,b,c,d)
constexpr int MkTag(unsigned a, unsigned b, unsigned c, unsigned d)
{
    return static_cast<int>(a | (b << 8) | (c << 16) | (d << 24));
}
constexpr int FfErrTag(unsigned a, unsigned b, unsigned c, unsigned d)
{
    return -MkTag(a, b, c, d);
}
} // namespace ffmpeg_error_detail

// Stable FFERRTAG-based error codes (independent of platform).
constexpr int kAvErrInvalidData = ffmpeg_error_detail::FfErrTag('I', 'N', 'D', 'A');
constexpr int kAvErrEof = ffmpeg_error_detail::FfErrTag('E', 'O', 'F', ' ');
constexpr int kAvErrDemuxerNotFound = ffmpeg_error_detail::FfErrTag(0xF8, 'D', 'E', 'M');
constexpr int kAvErrDecoderNotFound = ffmpeg_error_detail::FfErrTag(0xF8, 'D', 'E', 'C');
constexpr int kAvErrProtocolNotFound = ffmpeg_error_detail::FfErrTag(0xF8, 'P', 'R', 'O');
constexpr int kAvErrStreamNotFound = ffmpeg_error_detail::FfErrTag(0xF8, 'S', 'T', 'R');
constexpr int kAvErrBsfNotFound = ffmpeg_error_detail::FfErrTag(0xF8, 'B', 'S', 'F');

// errno-based codes: AVERROR(e) == -(e) on the platforms we target, with ENOENT==2 and
// EACCES==13 fixed by POSIX (and matched by the Windows CRT).
constexpr int kAvErrNoEnt = -2;  // AVERROR(ENOENT)
constexpr int kAvErrAccess = -13; // AVERROR(EACCES)

// Map a raw FFmpeg error code to a user-facing EndFileReason. Anything unrecognised
// stays the generic Error so behaviour never regresses for codes we don't classify.
inline EndFileReason ClassifyAvError(int err)
{
    switch (err)
    {
    case kAvErrNoEnt:
    case kAvErrAccess:
        return EndFileReason::NotFound;
    case kAvErrDemuxerNotFound:
    case kAvErrDecoderNotFound:
    case kAvErrProtocolNotFound:
    case kAvErrBsfNotFound:
        return EndFileReason::Unsupported;
    case kAvErrInvalidData:
    case kAvErrEof:
        return EndFileReason::Corrupt;
    case kAvErrStreamNotFound:
        return EndFileReason::NoStream;
    default:
        return EndFileReason::Error;
    }
}
