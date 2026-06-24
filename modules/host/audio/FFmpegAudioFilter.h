#pragma once

extern "C"
{
#include <libavutil/channel_layout.h>
#include <libavutil/rational.h>
#include <libavutil/samplefmt.h>
}

#include <string>

struct AVFrame;
struct AVFilterGraph;
struct AVFilterContext;

// Audio-filter graph for the FFmpeg backend (issue #8, Phase 6): wraps a libavfilter
// graph (abuffer → dynaudnorm,asoftclip,volume → abuffersink) used for dynamic audio
// normalization. The chain description is built natively by BuildAudioNormalizeGraph
// (FFmpegFilters.h); this class is the libav-touching half that actually runs frames
// through it. One of the few FFmpeg* files that may #include <libav*/...>.
//
// Lifetime: a worker-local object owned by FFmpegPlayer::AudioWorker. Configure() (re)builds
// the graph for the current decoder format; Send()/Receive() push and pull frames; Close()
// (and the destructor) free it. Not thread-safe — only the audio worker touches it.
class FFmpegAudioFilter
{
public:
    FFmpegAudioFilter() = default;
    ~FFmpegAudioFilter();

    FFmpegAudioFilter(const FFmpegAudioFilter&) = delete;
    FFmpegAudioFilter& operator=(const FFmpegAudioFilter&) = delete;

    // (Re)build the graph for input audio of the given format/time-base, running it through
    // graphDesc (e.g. "dynaudnorm=...,asoftclip=type=tanh,volume=..."). The abuffersink is
    // constrained to the same format so the downstream resampler stays valid. Frees any prior
    // graph first. Returns false on failure (the caller then bypasses filtering).
    bool Configure(int rate, const AVChannelLayout& layout, AVSampleFormat fmt, AVRational tb,
                   const std::string& graphDesc);

    // Push one decoded frame into the graph (frame == nullptr flushes / signals EOF).
    // Returns an AVERROR code (0 on success).
    int Send(AVFrame* in);
    // Pull one filtered frame. Returns 0 on success, AVERROR(EAGAIN) when none is ready,
    // AVERROR_EOF when drained. Caller loops until a non-zero return.
    int Receive(AVFrame* out);

    // Time-base of frames produced by the abuffersink (for pts → seconds).
    [[nodiscard]] AVRational OutputTimeBase() const;

    [[nodiscard]] bool IsActive() const
    {
        return graph_ != nullptr;
    }

    void Close();

private:
    AVFilterGraph* graph_ = nullptr;
    AVFilterContext* src_ = nullptr;  // abuffer
    AVFilterContext* sink_ = nullptr; // abuffersink
};
