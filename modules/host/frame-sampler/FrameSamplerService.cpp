#include "FrameSamplerService.h"

#include "SwsColorspace.h"

#include <framelift/Log.h>

#include <cstdint>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libavutil/pixfmt.h>
#include <libavutil/rational.h>
#include <libswscale/swscale.h>
}

// Everything a single sampling session holds. Software decode only (no hw device):
// predictable across hardware and free of contention with live playback. The RGBA
// conversion context is cached across ReadFrameRGBA calls (source format/size are
// immutable for a file; the destination size can vary, so it re-caches on change).
namespace
{
struct SamplerSession
{
    AVFormatContext* fmt = nullptr;
    AVCodecContext* dec = nullptr;
    AVStream* stream = nullptr;
    int vIdx = -1;
    int width = 0;
    int height = 0;
    double durationSec = 0.0;

    SwsContext* sws = nullptr;
    int swsSrcFmt = -1;
    int swsSrcW = 0;
    int swsSrcH = 0;
    int swsDstW = 0;
    int swsDstH = 0;
    int swsCoeff = -1;
    int swsSrcRange = -1;
};

void CloseSession(SamplerSession* s) noexcept
{
    if (!s)
    {
        return;
    }
    if (s->sws)
    {
        sws_freeContext(s->sws);
    }
    if (s->dec)
    {
        avcodec_free_context(&s->dec);
    }
    if (s->fmt)
    {
        avformat_close_input(&s->fmt);
    }
    delete s;
}
} // namespace

void* FrameSamplerService::Open(const char* path) noexcept
{
    if (!path || !*path)
    {
        return nullptr;
    }

    auto* s = new (std::nothrow) SamplerSession();
    if (!s)
    {
        return nullptr;
    }

    if (avformat_open_input(&s->fmt, path, nullptr, nullptr) < 0)
    {
        Log::Warn("FrameSampler: failed to open {}", path);
        CloseSession(s);
        return nullptr;
    }
    if (avformat_find_stream_info(s->fmt, nullptr) < 0)
    {
        Log::Warn("FrameSampler: no stream info for {}", path);
        CloseSession(s);
        return nullptr;
    }

    const AVCodec* codec = nullptr;
    s->vIdx = av_find_best_stream(s->fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (s->vIdx < 0 || !codec)
    {
        Log::Warn("FrameSampler: no video stream in {}", path);
        CloseSession(s);
        return nullptr;
    }
    s->stream = s->fmt->streams[s->vIdx];
    // Reject cover-art / attached-picture "video" streams (a single still packet on an
    // audio file) — they are not a timeline to sample.
    if ((s->stream->disposition & AV_DISPOSITION_ATTACHED_PIC) != 0)
    {
        Log::Warn("FrameSampler: {} has only an attached picture, not a video timeline", path);
        CloseSession(s);
        return nullptr;
    }

    s->dec = avcodec_alloc_context3(codec);
    if (!s->dec)
    {
        CloseSession(s);
        return nullptr;
    }
    avcodec_parameters_to_context(s->dec, s->stream->codecpar);
    s->dec->pkt_timebase = s->stream->time_base;
    s->dec->thread_count = 0; // auto for software decode
    if (avcodec_open2(s->dec, codec, nullptr) < 0 || s->dec->width <= 0 || s->dec->height <= 0)
    {
        Log::Warn("FrameSampler: could not open video decoder for {}", path);
        CloseSession(s);
        return nullptr;
    }
    s->width = s->dec->width;
    s->height = s->dec->height;

    // Duration: prefer the video stream's own; fall back to the container's.
    if (s->stream->duration != AV_NOPTS_VALUE && s->stream->duration > 0)
    {
        s->durationSec = static_cast<double>(s->stream->duration) * av_q2d(s->stream->time_base);
    }
    else if (s->fmt->duration != AV_NOPTS_VALUE && s->fmt->duration > 0)
    {
        s->durationSec = static_cast<double>(s->fmt->duration) / AV_TIME_BASE;
    }

    return s;
}

void FrameSamplerService::Close(void* session) noexcept
{
    CloseSession(static_cast<SamplerSession*>(session));
}

double FrameSamplerService::DurationSec(const void* session) const noexcept
{
    const auto* s = static_cast<const SamplerSession*>(session);
    return s ? s->durationSec : 0.0;
}

bool FrameSamplerService::NativeSize(const void* session, int* w, int* h) const noexcept
{
    const auto* s = static_cast<const SamplerSession*>(session);
    if (!s)
    {
        return false;
    }
    if (w)
    {
        *w = s->width;
    }
    if (h)
    {
        *h = s->height;
    }
    return true;
}

bool FrameSamplerService::ReadFrameRGBA(
    void* session, double posSec, int outW, int outH, unsigned char* buf, int cap, double* actualSec
) noexcept
{
    auto* s = static_cast<SamplerSession*>(session);
    if (!s || !s->dec || !s->stream || !buf)
    {
        return false;
    }

    const int dstW = (outW == 0 && outH == 0) ? s->width : outW;
    const int dstH = (outW == 0 && outH == 0) ? s->height : outH;
    if (dstW <= 0 || dstH <= 0)
    {
        return false;
    }
    const long long need = static_cast<long long>(dstW) * dstH * 4;
    if (static_cast<long long>(cap) < need)
    {
        return false;
    }

    // Clamp the request into the file and seek backward to the enclosing keyframe.
    double target = posSec;
    if (target < 0.0)
    {
        target = 0.0;
    }
    if (s->durationSec > 0.0 && target > s->durationSec)
    {
        target = s->durationSec;
    }
    const int64_t targetTs =
        av_rescale_q(static_cast<int64_t>(target * AV_TIME_BASE), AVRational{1, AV_TIME_BASE}, s->stream->time_base);

    if (av_seek_frame(s->fmt, s->vIdx, targetTs, AVSEEK_FLAG_BACKWARD) < 0)
    {
        // Some containers reject a per-stream seek; retry against the default stream.
        if (av_seek_frame(s->fmt, -1, static_cast<int64_t>(target * AV_TIME_BASE), AVSEEK_FLAG_BACKWARD) < 0)
        {
            return false;
        }
    }
    avcodec_flush_buffers(s->dec);

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    AVFrame* chosen = av_frame_alloc();
    if (!pkt || !frame || !chosen)
    {
        av_packet_free(&pkt);
        av_frame_free(&frame);
        av_frame_free(&chosen);
        return false;
    }

    bool haveChosen = false; // the frame we will return
    bool haveAny = false;    // any decoded frame (last-resort for seek-past-end)
    bool draining = false;

    auto framePtsSec = [&](const AVFrame* f) -> double
    {
        int64_t ts = f->best_effort_timestamp;
        if (ts == AV_NOPTS_VALUE)
        {
            ts = f->pts;
        }
        if (ts == AV_NOPTS_VALUE)
        {
            return 0.0;
        }
        return static_cast<double>(ts) * av_q2d(s->stream->time_base);
    };

    // Decode forward from the keyframe, keeping the last frame seen, until we reach the
    // first frame at/after the target (exact-seek: land on keyframe, then step forward).
    while (!haveChosen)
    {
        int readRet = 0;
        if (!draining)
        {
            readRet = av_read_frame(s->fmt, pkt);
            if (readRet < 0)
            {
                // End of input: flush the decoder to emit any buffered frames.
                avcodec_send_packet(s->dec, nullptr);
                draining = true;
            }
            else if (pkt->stream_index != s->vIdx)
            {
                av_packet_unref(pkt);
                continue;
            }
            else if (avcodec_send_packet(s->dec, pkt) < 0)
            {
                av_packet_unref(pkt);
                continue;
            }
            av_packet_unref(pkt);
        }

        int recvRet = avcodec_receive_frame(s->dec, frame);
        if (recvRet == AVERROR(EAGAIN))
        {
            if (draining)
            {
                break; // shouldn't happen while draining, but guard against a spin
            }
            continue;
        }
        if (recvRet == AVERROR_EOF)
        {
            break;
        }
        if (recvRet < 0)
        {
            break;
        }

        av_frame_unref(chosen);
        av_frame_move_ref(chosen, frame);
        haveAny = true;
        if (framePtsSec(chosen) >= target - 1e-4)
        {
            haveChosen = true;
        }
    }

    av_packet_free(&pkt);
    av_frame_free(&frame);

    if (!haveChosen && !haveAny)
    {
        av_frame_free(&chosen);
        return false;
    }
    // Seek past the last keyframe with no frame at/after target: return the final frame.

    const int srcFmt = chosen->format;
    if (s->sws == nullptr || s->swsSrcFmt != srcFmt || s->swsSrcW != chosen->width || s->swsSrcH != chosen->height ||
        s->swsDstW != dstW || s->swsDstH != dstH)
    {
        s->sws = sws_getCachedContext(
            s->sws, chosen->width, chosen->height, static_cast<AVPixelFormat>(srcFmt), dstW, dstH, AV_PIX_FMT_RGBA,
            SWS_BILINEAR, nullptr, nullptr, nullptr
        );
        s->swsSrcFmt = srcFmt;
        s->swsSrcW = chosen->width;
        s->swsSrcH = chosen->height;
        s->swsDstW = dstW;
        s->swsDstH = dstH;
        s->swsCoeff = -1; // force colorspace re-apply on the new context
    }
    if (!s->sws)
    {
        av_frame_free(&chosen);
        return false;
    }

    // Apply the YUV→RGB matrix on the CPU (the playback path defers this to the GPU
    // shader; a CPU RGBA consumer can't). Harmless for RGB-family sources.
    const int coeff = SwsColorspace::ResolveCoefficients(chosen->colorspace, chosen->height);
    const int srcRange = SwsColorspace::FullRange(chosen->color_range);
    if (coeff != s->swsCoeff || srcRange != s->swsSrcRange)
    {
        sws_setColorspaceDetails(
            s->sws, sws_getCoefficients(coeff), srcRange, sws_getCoefficients(SWS_CS_DEFAULT),
            /*dstRange=*/1, /*brightness=*/0, /*contrast=*/1 << 16, /*saturation=*/1 << 16
        );
        s->swsCoeff = coeff;
        s->swsSrcRange = srcRange;
    }

    uint8_t* dstData[4] = {buf, nullptr, nullptr, nullptr};
    int dstStride[4] = {dstW * 4, 0, 0, 0};
    const int scaled = sws_scale(s->sws, chosen->data, chosen->linesize, 0, chosen->height, dstData, dstStride);

    if (actualSec)
    {
        *actualSec = framePtsSec(chosen);
    }
    av_frame_free(&chosen);
    return scaled == dstH;
}
