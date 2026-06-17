#include "FFmpegSubtitles.h"

#include "FFmpegSubtitleBlend.h"

#include <framelift/Log.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

extern "C"
{
#include <ass/ass.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/mathematics.h>
#include <libavutil/rational.h>
}

namespace
{
void AssLogCallback(int level, const char* fmt, va_list va, void* /*ud*/)
{
    // libass levels: 0..7 (syslog-like). Map the noisy ones down to debug.
    if (level > 4)
    {
        return;
    }
    char buf[512];
    std::vsnprintf(buf, sizeof(buf), fmt, va);
    if (level <= 2)
    {
        Log::Warn("libass: {}", buf);
    }
    else
    {
        Log::Debug("libass: {}", buf);
    }
}
} // namespace

FFmpegSubtitles::FFmpegSubtitles()
{
    lib_ = ass_library_init();
    if (!lib_)
    {
        Log::Error("FFmpegSubtitles: ass_library_init failed; subtitles disabled");
        return;
    }
    ass_set_message_cb(lib_, AssLogCallback, nullptr);

    renderer_ = ass_renderer_init(lib_);
    if (!renderer_)
    {
        Log::Error("FFmpegSubtitles: ass_renderer_init failed; subtitles disabled");
        ass_library_done(lib_);
        lib_ = nullptr;
        return;
    }

    // Autodetect picks the platform font provider (DirectWrite on Windows,
    // fontconfig/CoreText elsewhere) and a sane default family.
    ass_set_fonts(renderer_, nullptr, "sans-serif", ASS_FONTPROVIDER_AUTODETECT, nullptr, 1);
    Log::Info("FFmpegSubtitles: libass {} ready", ass_library_version());
}

FFmpegSubtitles::~FFmpegSubtitles()
{
    std::lock_guard lock(mutex_);
    if (track_)
    {
        ass_free_track(track_);
        track_ = nullptr;
    }
    if (renderer_)
    {
        ass_renderer_done(renderer_);
        renderer_ = nullptr;
    }
    if (lib_)
    {
        ass_library_done(lib_);
        lib_ = nullptr;
    }
}

bool FFmpegSubtitles::Ok() const noexcept
{
    return lib_ != nullptr && renderer_ != nullptr;
}

void FFmpegSubtitles::BeginTrack(const unsigned char* header, int headerSize)
{
    std::lock_guard lock(mutex_);
    if (!lib_)
    {
        return;
    }
    if (track_)
    {
        ass_free_track(track_);
        track_ = nullptr;
    }
    track_ = ass_new_track(lib_);
    if (track_ && header && headerSize > 0)
    {
        ass_process_codec_private(track_, reinterpret_cast<char*>(const_cast<unsigned char*>(header)), headerSize);
    }
}

void FFmpegSubtitles::ProcessPacket(AVCodecContext* dec, AVPacket* pkt, int tbNum, int tbDen)
{
    std::lock_guard lock(mutex_);
    if (!track_ || !dec || !pkt || tbDen <= 0)
    {
        return;
    }

    AVSubtitle sub{};
    int got = 0;
    if (avcodec_decode_subtitle2(dec, &sub, &got, pkt) < 0 || !got)
    {
        return;
    }

    // Packet pts → milliseconds; AVSubtitle display times are ms relative to it.
    const AVRational tb{tbNum, tbDen};
    const int64_t ts = pkt->pts != AV_NOPTS_VALUE ? pkt->pts : pkt->dts;
    const long long baseMs = ts != AV_NOPTS_VALUE ? av_rescale_q(ts, tb, AVRational{1, 1000}) : 0;

    for (unsigned i = 0; i < sub.num_rects; ++i)
    {
        const AVSubtitleRect* r = sub.rects[i];
        if (r && r->type == SUBTITLE_ASS && r->ass)
        {
            const long long startMs = baseMs + sub.start_display_time;
            const long long durMs = static_cast<long long>(sub.end_display_time) - sub.start_display_time;
            ass_process_chunk(track_, r->ass, static_cast<int>(std::strlen(r->ass)), startMs, durMs);
        }
    }
    avsubtitle_free(&sub);
}

bool FFmpegSubtitles::PreloadFromFormatLocked(AVFormatContext* fmt)
{
    const AVCodec* codec = nullptr;
    const int sIdx = av_find_best_stream(fmt, AVMEDIA_TYPE_SUBTITLE, -1, -1, &codec, 0);
    if (sIdx < 0 || !codec)
    {
        return false;
    }
    AVStream* stream = fmt->streams[sIdx];
    AVCodecContext* dec = avcodec_alloc_context3(codec);
    if (!dec)
    {
        return false;
    }
    avcodec_parameters_to_context(dec, stream->codecpar);
    dec->pkt_timebase = stream->time_base;
    if (avcodec_open2(dec, codec, nullptr) < 0)
    {
        avcodec_free_context(&dec);
        return false;
    }

    if (track_)
    {
        ass_free_track(track_);
    }
    track_ = ass_new_track(lib_);
    if (track_ && dec->subtitle_header && dec->subtitle_header_size > 0)
    {
        ass_process_codec_private(track_, reinterpret_cast<char*>(dec->subtitle_header), dec->subtitle_header_size);
    }

    const AVRational tb = stream->time_base;
    AVPacket* pkt = av_packet_alloc();
    while (track_ && av_read_frame(fmt, pkt) >= 0)
    {
        if (pkt->stream_index == sIdx)
        {
            AVSubtitle sub{};
            int got = 0;
            if (avcodec_decode_subtitle2(dec, &sub, &got, pkt) >= 0 && got)
            {
                const int64_t ts = pkt->pts != AV_NOPTS_VALUE ? pkt->pts : pkt->dts;
                const long long baseMs = ts != AV_NOPTS_VALUE ? av_rescale_q(ts, tb, AVRational{1, 1000}) : 0;
                for (unsigned i = 0; i < sub.num_rects; ++i)
                {
                    const AVSubtitleRect* r = sub.rects[i];
                    if (r && r->type == SUBTITLE_ASS && r->ass)
                    {
                        ass_process_chunk(track_, r->ass, static_cast<int>(std::strlen(r->ass)),
                                          baseMs + sub.start_display_time,
                                          static_cast<long long>(sub.end_display_time) - sub.start_display_time);
                    }
                }
                avsubtitle_free(&sub);
            }
        }
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    avcodec_free_context(&dec);
    return true;
}

bool FFmpegSubtitles::LoadExternalFile(const char* path)
{
    std::lock_guard lock(mutex_);
    if (!lib_ || !path)
    {
        return false;
    }
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path, nullptr, nullptr) < 0)
    {
        Log::Warn("FFmpegSubtitles: failed to open external subtitle {}", path);
        return false;
    }
    bool ok = false;
    if (avformat_find_stream_info(fmt, nullptr) >= 0)
    {
        ok = PreloadFromFormatLocked(fmt);
    }
    avformat_close_input(&fmt);
    if (!ok)
    {
        Log::Warn("FFmpegSubtitles: no usable subtitle stream in {}", path);
    }
    return ok;
}

void FFmpegSubtitles::FlushEvents()
{
    std::lock_guard lock(mutex_);
    if (track_)
    {
        ass_flush_events(track_);
    }
}

void FFmpegSubtitles::ClearTrack()
{
    std::lock_guard lock(mutex_);
    if (track_)
    {
        ass_free_track(track_);
        track_ = nullptr;
    }
}

void FFmpegSubtitles::ApplyStyle(const SubtitleStyle& style)
{
    std::lock_guard lock(mutex_);
    style_ = style;
    forceNextUpdate_ = true; // re-rasterize the current frame on the next render
    ApplyStyleLocked();
}

void FFmpegSubtitles::ApplyStyleLocked()
{
    if (!renderer_)
    {
        return;
    }

    if (!style_.overrideEnabled)
    {
        // Use the file's embedded styling verbatim.
        ass_set_selective_style_override_enabled(renderer_, 0);
        ass_set_font_scale(renderer_, 1.0);
        ass_set_line_spacing(renderer_, 0.0);
        return;
    }

    // Override applies on top of the track's "Default" style (mpv's ass-override model).
    ASS_Style ov{};
    int bits = ASS_OVERRIDE_BIT_SELECTIVE_FONT_SCALE;

    if (style_.fontFamily[0] != '\0')
    {
        ov.FontName = const_cast<char*>(style_.fontFamily);
        bits |= ASS_OVERRIDE_BIT_FONT_NAME;
    }

    ov.PrimaryColour = style_.textColor;
    ov.OutlineColour = style_.outlineColor;
    ov.BackColour = style_.backColor;
    bits |= ASS_OVERRIDE_BIT_COLORS;

    switch (style_.edgeStyle)
    {
    case SubtitleEdgeStyle::None:
        ov.BorderStyle = 1;
        ov.Outline = 0.0;
        ov.Shadow = 0.0;
        break;
    case SubtitleEdgeStyle::Outline:
        ov.BorderStyle = 1;
        ov.Outline = style_.outlineWidth;
        ov.Shadow = 0.0;
        break;
    case SubtitleEdgeStyle::DropShadow:
        ov.BorderStyle = 1;
        ov.Outline = style_.outlineWidth;
        ov.Shadow = style_.shadowDepth;
        break;
    case SubtitleEdgeStyle::OpaqueBox:
        ov.BorderStyle = 3;
        ov.Outline = style_.outlineWidth;
        ov.Shadow = style_.shadowDepth;
        break;
    }
    bits |= ASS_OVERRIDE_BIT_BORDER;

    if (style_.alignment >= 1 && style_.alignment <= 9)
    {
        ov.Alignment = style_.alignment;
        bits |= ASS_OVERRIDE_BIT_ALIGNMENT;
    }

    ov.Spacing = style_.letterSpacing;

    ass_set_selective_style_override(renderer_, &ov);
    ass_set_selective_style_override_enabled(renderer_, bits);
    ass_set_font_scale(renderer_, style_.fontScale > 0.f ? style_.fontScale : 1.0);
    ass_set_line_spacing(renderer_, style_.lineSpacing);
}

FFmpegSubtitles::RenderResult FFmpegSubtitles::RenderOverlay(int vpW, int vpH, int storageW, int storageH,
                                                             long long timeMs, std::vector<unsigned char>& outRgba)
{
    std::lock_guard lock(mutex_);
    if (!renderer_ || !track_ || vpW <= 0 || vpH <= 0)
    {
        return RenderResult::None;
    }

    if (storageW > 0 && storageH > 0)
    {
        ass_set_storage_size(renderer_, storageW, storageH);
    }
    ass_set_frame_size(renderer_, vpW, vpH);

    int changed = 0;
    ASS_Image* img = ass_render_frame(renderer_, track_, timeMs, &changed);
    if (!img)
    {
        return RenderResult::None;
    }
    const bool forced = forceNextUpdate_;
    forceNextUpdate_ = false;
    if (!forced && changed == 0 && outRgba.size() == static_cast<size_t>(vpW) * vpH * 4)
    {
        // Identical to the previous frame and our buffer still matches — let the
        // caller reuse the already-uploaded texture.
        return RenderResult::Unchanged;
    }

    outRgba.assign(static_cast<size_t>(vpW) * vpH * 4, 0);
    for (; img; img = img->next)
    {
        BlendCoverageBitmap(outRgba.data(), vpW, vpH, img->bitmap, img->w, img->h, img->stride, img->dst_x, img->dst_y,
                            img->color);
    }
    return RenderResult::Updated;
}
