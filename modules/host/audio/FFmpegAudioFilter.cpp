#include "FFmpegAudioFilter.h"

#include <framelift/Log.h>

extern "C"
{
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
}

#include <cstdio>

FFmpegAudioFilter::~FFmpegAudioFilter()
{
    Close();
}

bool FFmpegAudioFilter::Configure(int rate, const AVChannelLayout& layout, AVSampleFormat fmt, AVRational tb,
                                  const std::string& graphDesc)
{
    Close();

    const AVFilter* abuffersrc = avfilter_get_by_name("abuffer");
    const AVFilter* abuffersink = avfilter_get_by_name("abuffersink");
    graph_ = avfilter_graph_alloc();
    if (!abuffersrc || !abuffersink || !graph_)
    {
        Log::Error("FFmpegAudioFilter: failed to allocate filter graph");
        Close();
        return false;
    }

    char layoutStr[64] = {};
    av_channel_layout_describe(&layout, layoutStr, sizeof(layoutStr));

    // abuffer source: feeds decoded frames into the graph in their native format.
    char args[256];
    std::snprintf(args, sizeof(args), "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=%s", tb.num, tb.den,
                  rate, av_get_sample_fmt_name(fmt), layoutStr);
    if (avfilter_graph_create_filter(&src_, abuffersrc, "in", args, nullptr, graph_) < 0)
    {
        Log::Error("FFmpegAudioFilter: failed to create abuffer source");
        Close();
        return false;
    }

    // abuffersink: left unconstrained. The output format is instead pinned to the input
    // format by appending an `aformat` filter to the parsed chain (below) — the portable,
    // non-deprecated way to negotiate the format, so the downstream SwrContext stays valid.
    if (avfilter_graph_create_filter(&sink_, abuffersink, "out", nullptr, nullptr, graph_) < 0)
    {
        Log::Error("FFmpegAudioFilter: failed to create abuffersink");
        Close();
        return false;
    }

    // Pin the chain's output back to the decoder's format with a trailing aformat, so the
    // intermediate stages (dynaudnorm runs in dblp) don't change what reaches the sink.
    const std::string fullDesc = graphDesc + ",aformat=sample_fmts=" + av_get_sample_fmt_name(fmt) +
                                 ":sample_rates=" + std::to_string(rate) + ":channel_layouts=" + layoutStr;

    // Connect: src (labelled "in") → parsed chain → sink (labelled "out"). The
    // inout naming follows the libavfilter convention (outputs == graph endpoints
    // the parsed string reads from; inputs == endpoints it writes to).
    AVFilterInOut* outputs = avfilter_inout_alloc();
    AVFilterInOut* inputs = avfilter_inout_alloc();
    if (!outputs || !inputs)
    {
        avfilter_inout_free(&outputs);
        avfilter_inout_free(&inputs);
        Close();
        return false;
    }
    outputs->name = av_strdup("in");
    outputs->filter_ctx = src_;
    outputs->pad_idx = 0;
    outputs->next = nullptr;
    inputs->name = av_strdup("out");
    inputs->filter_ctx = sink_;
    inputs->pad_idx = 0;
    inputs->next = nullptr;

    int ret = avfilter_graph_parse_ptr(graph_, fullDesc.c_str(), &inputs, &outputs, nullptr);
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    if (ret < 0)
    {
        Log::Error("FFmpegAudioFilter: failed to parse graph '{}'", fullDesc);
        Close();
        return false;
    }
    if (avfilter_graph_config(graph_, nullptr) < 0)
    {
        Log::Error("FFmpegAudioFilter: failed to configure graph");
        Close();
        return false;
    }

    Log::Info("FFmpegAudioFilter: configured graph [{}]", graphDesc);
    return true;
}

int FFmpegAudioFilter::Send(AVFrame* in)
{
    if (!src_)
    {
        return -1;
    }
    // KEEP_REF leaves the caller's frame ref intact so the worker can still unref it.
    return av_buffersrc_add_frame_flags(src_, in, AV_BUFFERSRC_FLAG_KEEP_REF);
}

int FFmpegAudioFilter::Receive(AVFrame* out)
{
    if (!sink_)
    {
        return -1;
    }
    return av_buffersink_get_frame(sink_, out);
}

AVRational FFmpegAudioFilter::OutputTimeBase() const
{
    if (!sink_)
    {
        return {0, 1};
    }
    return av_buffersink_get_time_base(sink_);
}

void FFmpegAudioFilter::Close()
{
    if (graph_)
    {
        avfilter_graph_free(&graph_);
    }
    src_ = nullptr;
    sink_ = nullptr;
}
