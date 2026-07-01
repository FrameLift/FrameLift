#include "FFmpegAudioOutput.h"

#include "FFmpegAudioOptions.h"
#include "FFmpegClock.h"

#include <framelift/Log.h>

extern "C"
{
#include <libavutil/frame.h>
#include <libavutil/mathematics.h>
#include <libswresample/swresample.h>
}

#include <QtCore/QIODevice>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QThread>
#include <QtMultimedia/QAudioDevice>
#include <QtMultimedia/QAudioFormat>
#include <QtMultimedia/QAudioSink>
#include <QtMultimedia/QMediaDevices>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <vector>

namespace
{
// The QAudioSink's internal buffer adds latency our ring-fill measure doesn't see (the
// sink pulls ahead of playback). We keep that buffer small and treat it as ~full when
// computing the clock. Verify lip-sync on a long file before changing this value.
constexpr double kSinkBufferSeconds = 0.10;

// Thread-safe byte FIFO bridging the audio worker (writer) and the QAudioSink pull
// (reader, on the audio thread). Its own mutex makes it independent of the outer class
// lock so a sink pull never contends with a control call.
class AudioRing
{
public:
    void Write(const uint8_t* data, size_t n)
    {
        std::lock_guard lock(m_);
        buf_.insert(buf_.end(), data, data + n);
    }

    // Reader side (QAudioSink pull). Returns the real bytes copied (may be < n on
    // underrun — the sink then idles and retries, so no silence is fabricated and the
    // clock stays honest).
    size_t Read(uint8_t* out, size_t n)
    {
        std::lock_guard lock(m_);
        const size_t avail = buf_.size() - readPos_;
        const size_t take = std::min(n, avail);
        if (take > 0)
        {
            std::memcpy(out, buf_.data() + readPos_, take);
            readPos_ += take;
            // Compact once the consumed prefix dominates, to bound memory.
            if (readPos_ > (1u << 16) && readPos_ * 2 >= buf_.size())
            {
                buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(readPos_));
                readPos_ = 0;
            }
        }
        return take;
    }

    [[nodiscard]] size_t Available() const
    {
        std::lock_guard lock(m_);
        return buf_.size() - readPos_;
    }

    void Clear()
    {
        std::lock_guard lock(m_);
        buf_.clear();
        readPos_ = 0;
    }

private:
    mutable std::mutex m_;
    std::vector<uint8_t> buf_;
    size_t readPos_ = 0;
};

// QIODevice the QAudioSink pulls from (opened read-only, sequential). Lives on the audio
// thread; reads straight from the ring.
class AudioRingDevice final : public QIODevice
{
public:
    explicit AudioRingDevice(AudioRing* ring) : ring_(ring) {}

    [[nodiscard]] bool isSequential() const override { return true; }
    [[nodiscard]] qint64 bytesAvailable() const override
    {
        return static_cast<qint64>(ring_->Available()) + QIODevice::bytesAvailable();
    }

protected:
    qint64 readData(char* data, qint64 maxlen) override
    {
        if (maxlen <= 0)
        {
            return 0;
        }
        return static_cast<qint64>(ring_->Read(reinterpret_cast<uint8_t*>(data), static_cast<size_t>(maxlen)));
    }
    qint64 writeData(const char*, qint64) override { return -1; }

private:
    AudioRing* ring_;
};

QAudioDevice PickDevice(const QString& description)
{
    if (description.isEmpty())
    {
        return QMediaDevices::defaultAudioOutput();
    }
    for (const QAudioDevice& d : QMediaDevices::audioOutputs())
    {
        if (d.description() == description)
        {
            return d;
        }
    }
    return QMediaDevices::defaultAudioOutput();
}
} // namespace

// The Qt backend: a QAudioSink + its pull device on a dedicated event-loop thread, plus
// the ring the worker feeds. All QAudioSink touches happen on thread_ (marshalled via ctx_).
struct AudioSink
{
    AudioRing ring;
    QThread thread;
    QObject* ctx = nullptr;        // invoke target with thread_ affinity
    QAudioSink* sink = nullptr;    // created/used on thread_
    AudioRingDevice* device = nullptr;
    std::atomic<bool> active{false};
    int bufferBytes = 0; // sink internal buffer size (for the clock latency term)

    template <class Fn> void RunOnThread(Fn&& fn, bool blocking)
    {
        if (!ctx)
        {
            return;
        }
        QMetaObject::invokeMethod(ctx, std::forward<Fn>(fn),
                                  blocking ? Qt::BlockingQueuedConnection : Qt::QueuedConnection);
    }

    // Bring up the sink for (rate, channels). Returns false if the format is unsupported
    // (caller may retry with fewer channels) or device open failed.
    bool Start(int rate, int channels, const QString& preferred, float gain, int wantBufferBytes)
    {
        if (!thread.isRunning())
        {
            thread.start();
        }
        if (!ctx)
        {
            ctx = new QObject();
            ctx->moveToThread(&thread);
        }

        bool ok = false;
        RunOnThread(
            [&]
            {
                QAudioFormat fmt;
                fmt.setSampleRate(rate);
                fmt.setChannelCount(channels);
                fmt.setSampleFormat(QAudioFormat::Float);

                const QAudioDevice dev = PickDevice(preferred);
                if (!dev.isFormatSupported(fmt))
                {
                    ok = false;
                    return;
                }
                ring.Clear();
                device = new AudioRingDevice(&ring);
                device->open(QIODevice::ReadOnly);
                sink = new QAudioSink(dev, fmt);
                if (wantBufferBytes > 0)
                {
                    sink->setBufferSize(wantBufferBytes);
                }
                sink->setVolume(gain);
                sink->start(device);
                bufferBytes = sink->bufferSize();
                ok = sink->error() == QAudio::NoError;
                if (!ok)
                {
                    sink->stop();
                    delete sink;
                    sink = nullptr;
                    device->close();
                    delete device;
                    device = nullptr;
                }
            },
            /*blocking=*/true);

        active = ok;
        return ok;
    }

    void Stop()
    {
        if (ctx)
        {
            RunOnThread(
                [this]
                {
                    if (sink)
                    {
                        sink->stop();
                        delete sink;
                        sink = nullptr;
                    }
                    if (device)
                    {
                        device->close();
                        delete device;
                        device = nullptr;
                    }
                },
                /*blocking=*/true);
        }
        active = false;
        if (thread.isRunning())
        {
            thread.quit();
            thread.wait();
        }
        delete ctx; // thread finished; no pending events
        ctx = nullptr;
    }

    void SetGain(float g)
    {
        RunOnThread([this, g] { if (sink) sink->setVolume(g); }, /*blocking=*/false);
    }

    void SetSuspended(bool s)
    {
        RunOnThread(
            [this, s]
            {
                if (!sink)
                {
                    return;
                }
                if (s)
                {
                    sink->suspend();
                }
                else
                {
                    sink->resume();
                }
            },
            /*blocking=*/false);
    }

    // Drop the sink's internal buffer (seek) and restart pulling from the (cleared) ring.
    void ResetSink()
    {
        RunOnThread(
            [this]
            {
                if (sink && device)
                {
                    sink->reset();
                    sink->start(device);
                }
            },
            /*blocking=*/true);
    }

    ~AudioSink() { Stop(); }
};

// ── FFmpegAudioOutput ─────────────────────────────────────────────────────────

FFmpegAudioOutput::FFmpegAudioOutput() = default;

FFmpegAudioOutput::~FFmpegAudioOutput()
{
    Close();
}

bool FFmpegAudioOutput::Open(int srcRate, const AVChannelLayout& srcLayout, AVSampleFormat srcFmt)
{
    std::lock_guard lock(mutex_);

    const int newRate = srcRate;
    const int newChannels = DesiredChannelsLocked();

    // Reuse a still-running sink when the next file's output format is identical (same
    // sample rate, channel count, and selected device). Closing/reopening the QAudioSink
    // and its dedicated thread is the bulk of the audio open cost on a file boundary; only
    // the resampler — which depends on the *source* format — must be rebuilt each time.
    const bool reuse = sink_ && sink_->active.load() && dstRate_ == newRate && dstChannels_ == newChannels &&
                       sinkDevice_ == preferredDevice_;
    if (!reuse)
    {
        CloseLocked();
    }

    dstRate_ = newRate;
    dstChannels_ = newChannels;

    // Build the resampler to F32 interleaved / dstChannels_ / dstRate_, retrying at stereo
    // if a surround sink isn't available.
    auto buildSwr = [&](int channels) -> bool
    {
        if (swr_)
        {
            swr_free(&swr_); // drop the previous file's resampler (kept across a reuse)
        }
        AVChannelLayout dstLayout;
        av_channel_layout_default(&dstLayout, channels);
        SwrContext* swr = nullptr;
        const int ret =
            swr_alloc_set_opts2(&swr, &dstLayout, AV_SAMPLE_FMT_FLT, dstRate_, &srcLayout, srcFmt, srcRate, 0, nullptr);
        av_channel_layout_uninit(&dstLayout);
        if (ret < 0 || !swr || swr_init(swr) < 0)
        {
            swr_free(&swr);
            return false;
        }
        swr_ = swr;
        return true;
    };

    if (!buildSwr(dstChannels_))
    {
        Log::Error("FFmpegAudioOutput: resampler init failed");
        CloseLocked();
        return false;
    }
    bytesPerSec_ = dstRate_ * dstChannels_ * static_cast<int>(sizeof(float));

    if (reuse)
    {
        // Sink + thread stay up; just discard the previous file's queued audio so the new
        // file's samples start playing from a clean baseline.
        sink_->ring.Clear();
        sink_->ResetSink();
        lastQueuedPts_ = 0.0;
        return true;
    }

    sink_ = std::make_unique<AudioSink>();
    const QString preferred = QString::fromStdString(preferredDevice_);
    const int bufBytes = static_cast<int>(static_cast<double>(bytesPerSec_) * kSinkBufferSeconds);
    bool ok = sink_->Start(dstRate_, dstChannels_, preferred, CurrentGainLocked(), bufBytes);

    if (!ok && dstChannels_ == 6)
    {
        Log::Warn("FFmpegAudioOutput: surround output unavailable, falling back to stereo");
        swr_free(&swr_);
        dstChannels_ = 2;
        if (buildSwr(dstChannels_))
        {
            bytesPerSec_ = dstRate_ * dstChannels_ * static_cast<int>(sizeof(float));
            ok = sink_->Start(dstRate_, dstChannels_, preferred, CurrentGainLocked(), bufBytes);
        }
    }

    if (!ok)
    {
        Log::Error("FFmpegAudioOutput: QAudioSink start failed");
        CloseLocked();
        return false;
    }

    sinkDevice_ = preferredDevice_;
    lastQueuedPts_ = 0.0;
    Log::Debug("FFmpegAudioOutput: opened {} Hz {}ch F32 (QAudioSink)", dstRate_, dstChannels_);
    return true;
}

void FFmpegAudioOutput::Feed(const AVFrame* frame, double ptsSec)
{
    std::lock_guard lock(mutex_);
    if (!sink_ || !sink_->active.load() || !swr_ || !frame)
    {
        return;
    }

    // Decay a pending audio duck: the audio worker drives this regularly, so the host
    // needs no per-frame tick to restore the gain after the pulse expires.
    if (ducked_ && std::chrono::steady_clock::now() >= duckUntil_)
    {
        ducked_ = false;
        ApplyGainLocked();
    }

    const int inRate = frame->sample_rate > 0 ? frame->sample_rate : dstRate_;
    const int64_t maxOut =
        av_rescale_rnd(swr_get_delay(swr_, inRate) + frame->nb_samples, dstRate_, inRate, AV_ROUND_UP);
    if (maxOut <= 0)
    {
        return;
    }

    const auto byteCap = static_cast<size_t>(maxOut) * dstChannels_ * sizeof(float);
    if (buffer_.size() < byteCap)
    {
        buffer_.resize(byteCap);
    }

    uint8_t* outPtr = buffer_.data();
    const int converted = swr_convert(swr_, &outPtr, static_cast<int>(maxOut),
                                      const_cast<const uint8_t**>(frame->extended_data), frame->nb_samples);
    if (converted <= 0)
    {
        return;
    }

    const size_t bytes = static_cast<size_t>(converted) * dstChannels_ * sizeof(float);
    sink_->ring.Write(buffer_.data(), bytes);
    lastQueuedPts_ = ptsSec + static_cast<double>(converted) / static_cast<double>(dstRate_);
}

double FFmpegAudioOutput::MasterClock() const
{
    std::lock_guard lock(mutex_);
    return ComputeMasterClock(lastQueuedPts_, QueuedBytesLocked(), bytesPerSec_);
}

int64_t FFmpegAudioOutput::QueuedBytes() const
{
    std::lock_guard lock(mutex_);
    return QueuedBytesLocked();
}

int64_t FFmpegAudioOutput::QueuedBytesLocked() const
{
    if (!sink_)
    {
        return 0;
    }
    // Unheard bytes = still in the ring + the sink's internal buffer (it pulls ahead and
    // keeps that ~full during steady playback).
    return static_cast<int64_t>(sink_->ring.Available()) + sink_->bufferBytes;
}

bool FFmpegAudioOutput::HasDevice() const
{
    std::lock_guard lock(mutex_);
    return sink_ && sink_->active.load();
}

void FFmpegAudioOutput::SetPaused(bool paused)
{
    std::lock_guard lock(mutex_);
    if (sink_)
    {
        sink_->SetSuspended(paused);
    }
}

void FFmpegAudioOutput::SetVolume(int volume0to100)
{
    std::lock_guard lock(mutex_);
    volume_ = std::clamp(volume0to100, 0, 100);
    ApplyGainLocked();
}

void FFmpegAudioOutput::SetMute(bool muted)
{
    std::lock_guard lock(mutex_);
    muted_ = muted;
    ApplyGainLocked();
}

void FFmpegAudioOutput::SetPreferences(const AudioPreferences& prefs)
{
    std::lock_guard lock(mutex_);
    preferredDevice_ = prefs.outputDevice;
    channelMode_ = prefs.channelMode;
    duckingEnabled_ = prefs.duckingEnabled;
    duckingLevel_ = std::clamp(prefs.duckingLevel, 0, 100);
    ApplyGainLocked();
}

void FFmpegAudioOutput::PulseDuck()
{
    std::lock_guard lock(mutex_);
    ducked_ = true;
    duckUntil_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(800);
    ApplyGainLocked();
}

void FFmpegAudioOutput::EnumerateDevices(void (*visit)(const AudioOutputDevice* device, void* ud), void* ud) const
{
    if (!visit)
    {
        return;
    }

    std::string selected;
    {
        std::lock_guard lock(mutex_);
        selected = preferredDevice_;
    }

    AudioOutputDevice def{};
    def.isDefault = true;
    def.selected = selected.empty();
    visit(&def, ud);

    for (const QAudioDevice& d : QMediaDevices::audioOutputs())
    {
        const std::string name = d.description().toStdString();
        if (name.empty())
        {
            continue;
        }
        AudioOutputDevice out{};
        std::strncpy(out.name, name.c_str(), sizeof(out.name) - 1);
        out.selected = selected == name;
        visit(&out, ud);
    }
}

int FFmpegAudioOutput::DesiredChannelsLocked() const
{
    return AudioOutputChannelsForMode(channelMode_);
}

float FFmpegAudioOutput::CurrentGainLocked() const
{
    float gain = muted_ ? 0.0f : static_cast<float>(volume_) / 100.0f;
    if (!muted_ && duckingEnabled_ && ducked_)
    {
        gain *= static_cast<float>(duckingLevel_) / 100.0f;
    }
    return gain;
}

void FFmpegAudioOutput::ApplyGainLocked()
{
    if (sink_)
    {
        sink_->SetGain(CurrentGainLocked());
    }
}

void FFmpegAudioOutput::Flush()
{
    std::lock_guard lock(mutex_);
    if (sink_)
    {
        sink_->ring.Clear();
        sink_->ResetSink();
    }
    lastQueuedPts_ = 0.0;
}

void FFmpegAudioOutput::Close()
{
    std::lock_guard lock(mutex_);
    CloseLocked();
}

void FFmpegAudioOutput::CloseLocked()
{
    sink_.reset(); // tears down the sink + audio thread (AudioSink::~AudioSink → Stop)
    if (swr_)
    {
        swr_free(&swr_);
    }
    bytesPerSec_ = 0;
    lastQueuedPts_ = 0.0;
    sinkDevice_.clear();
}
