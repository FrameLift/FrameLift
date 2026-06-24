#pragma once

#include <framelift/platform/IMediaPlayer.h>

#include <string>

// Pure builder for the FFmpeg backend's audio-normalization filter graph (issue #8,
// Phase 6). Kept out of FFmpegAudioFilter.cpp (which needs the libavfilter headers) so
// the chain-building logic can be unit-tested natively. Takes only POD types from
// IMediaPlayer.h.

// Builds the libavfilter chain description for dynamic audio normalization, passed to
// avfilter_graph_parse_ptr between the abuffer source and abuffersink. There is no
// "lavfi=[...]" wrapper — the buffer/buffersink endpoints are added when the graph is
// constructed. The dynaudnorm gaussian window must be odd, so gaussSize is rounded up
// via | 1.
inline std::string BuildAudioNormalizeGraph(const AudioNormalizeParams& params)
{
    const int g = params.gaussSize | 1; // dynaudnorm requires an odd gaussian window
    return "dynaudnorm=f=" + std::to_string(params.frameLen) + ":g=" + std::to_string(g) +
           ":p=" + std::to_string(params.peak) + ":m=" + std::to_string(params.maxGain) +
           ",asoftclip=type=tanh,volume=" + std::to_string(params.volume);
}
