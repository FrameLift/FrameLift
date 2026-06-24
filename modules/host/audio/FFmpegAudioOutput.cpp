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

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>

FFmpegAudioOutput::FFmpegAudioOutput()
{
    // Refcounted — the host already holds an SDL_INIT_AUDIO ref; this keeps the
    // class self-contained if that ever changes.
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
    {
        Log::Warn("FFmpegAudioOutput: SDL_InitSubSystem(AUDIO) failed: {}", SDL_GetError());
    }
}

FFmpegAudioOutput::~FFmpegAudioOutput()
{
    Close();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

bool FFmpegAudioOutput::Open(int srcRate, const AVChannelLayout& srcLayout, AVSampleFormat srcFmt)
{
    Close();

    std::lock_guard lock(mutex_);

    dstRate_ = srcRate;
    dstChannels_ = DesiredChannelsLocked();
    bytesPerSec_ = dstRate_ * dstChannels_ * static_cast<int>(sizeof(float));

    AVChannelLayout dstLayout;
    av_channel_layout_default(&dstLayout, dstChannels_);
    SwrContext* swr = nullptr;
    const int ret = swr_alloc_set_opts2(
        &swr, &dstLayout, AV_SAMPLE_FMT_FLT, dstRate_, &srcLayout, srcFmt, srcRate, 0, nullptr
    );
    av_channel_layout_uninit(&dstLayout);
    if (ret < 0 || !swr)
    {
        Log::Error("FFmpegAudioOutput: swr_alloc_set_opts2 failed");
        bytesPerSec_ = 0;
        return false;
    }
    if (swr_init(swr) < 0)
    {
        Log::Error("FFmpegAudioOutput: swr_init failed");
        swr_free(&swr);
        bytesPerSec_ = 0;
        return false;
    }
    swr_ = swr;

    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32;
    spec.channels = dstChannels_;
    spec.freq = dstRate_;

    SDL_AudioDeviceID deviceId = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
    if (!preferredDevice_.empty())
    {
        int count = 0;
        SDL_AudioDeviceID* devices = SDL_GetAudioPlaybackDevices(&count);
        for (int i = 0; devices && i < count; ++i)
        {
            const char* name = SDL_GetAudioDeviceName(devices[i]);
            if (name && preferredDevice_ == name)
            {
                deviceId = devices[i];
                break;
            }
        }
        SDL_free(devices);
    }

    stream_ = SDL_OpenAudioDeviceStream(deviceId, &spec, nullptr, nullptr);
    if (!stream_ && dstChannels_ == 6)
    {
        Log::Warn("FFmpegAudioOutput: surround output failed, falling back to stereo: {}", SDL_GetError());
        swr_free(&swr_);
        dstChannels_ = 2;
        bytesPerSec_ = dstRate_ * dstChannels_ * static_cast<int>(sizeof(float));
        av_channel_layout_default(&dstLayout, dstChannels_);
        SwrContext* fallbackSwr = nullptr;
        const int fallbackRet = swr_alloc_set_opts2(
            &fallbackSwr, &dstLayout, AV_SAMPLE_FMT_FLT, dstRate_, &srcLayout, srcFmt, srcRate, 0, nullptr
        );
        av_channel_layout_uninit(&dstLayout);
        if (fallbackRet < 0 || !fallbackSwr || swr_init(fallbackSwr) < 0)
        {
            Log::Error("FFmpegAudioOutput: stereo fallback resampler failed");
            swr_free(&fallbackSwr);
            bytesPerSec_ = 0;
            return false;
        }
        swr_ = fallbackSwr;
        spec.channels = dstChannels_;
        stream_ = SDL_OpenAudioDeviceStream(deviceId, &spec, nullptr, nullptr);
    }
    if (!stream_ && deviceId != SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK)
    {
        Log::Warn("FFmpegAudioOutput: preferred device '{}' failed, falling back to system default: {}",
                  preferredDevice_, SDL_GetError());
        stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    }
    if (!stream_)
    {
        Log::Error("FFmpegAudioOutput: SDL_OpenAudioDeviceStream failed: {}", SDL_GetError());
        swr_free(&swr_);
        bytesPerSec_ = 0;
        return false;
    }

    lastQueuedPts_ = 0.0;
    ApplyGainLocked();
    SDL_ResumeAudioStreamDevice(stream_);
    Log::Info("FFmpegAudioOutput: opened {} Hz {}ch F32", dstRate_, dstChannels_);
    return true;
}

void FFmpegAudioOutput::Feed(const AVFrame* frame, double ptsSec)
{
    std::lock_guard lock(mutex_);
    if (!stream_ || !swr_ || !frame)
    {
        return;
    }

    // Decay a pending audio duck: the audio worker drives this regularly, so the
    // host needs no per-frame tick to restore the gain after the pulse expires.
    if (ducked_ && std::chrono::steady_clock::now() >= duckUntil_)
    {
        ducked_ = false;
        ApplyGainLocked();
    }

    const int inRate = frame->sample_rate > 0 ? frame->sample_rate : dstRate_;
    // Output sample count incl. samples still buffered inside the resampler.
    const int64_t maxOut = av_rescale_rnd(swr_get_delay(swr_, inRate) + frame->nb_samples, dstRate_, inRate, AV_ROUND_UP);
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
    const int converted = swr_convert(
        swr_, &outPtr, static_cast<int>(maxOut), const_cast<const uint8_t**>(frame->extended_data), frame->nb_samples
    );
    if (converted <= 0)
    {
        return;
    }

    const int bytes = converted * dstChannels_ * static_cast<int>(sizeof(float));
    if (!SDL_PutAudioStreamData(stream_, buffer_.data(), bytes))
    {
        Log::Warn("FFmpegAudioOutput: SDL_PutAudioStreamData failed: {}", SDL_GetError());
        return;
    }

    lastQueuedPts_ = ptsSec + static_cast<double>(converted) / static_cast<double>(dstRate_);
}

double FFmpegAudioOutput::MasterClock() const
{
    std::lock_guard lock(mutex_);
    const int64_t queued = stream_ ? SDL_GetAudioStreamQueued(stream_) : 0;
    return ComputeMasterClock(lastQueuedPts_, queued, bytesPerSec_);
}

int64_t FFmpegAudioOutput::QueuedBytes() const
{
    std::lock_guard lock(mutex_);
    return stream_ ? SDL_GetAudioStreamQueued(stream_) : 0;
}

bool FFmpegAudioOutput::HasDevice() const
{
    std::lock_guard lock(mutex_);
    return stream_ != nullptr;
}

void FFmpegAudioOutput::SetPaused(bool paused)
{
    std::lock_guard lock(mutex_);
    if (!stream_)
    {
        return;
    }
    if (paused)
    {
        SDL_PauseAudioStreamDevice(stream_);
    }
    else
    {
        SDL_ResumeAudioStreamDevice(stream_);
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

    int count = 0;
    SDL_AudioDeviceID* devices = SDL_GetAudioPlaybackDevices(&count);
    for (int i = 0; devices && i < count; ++i)
    {
        const char* name = SDL_GetAudioDeviceName(devices[i]);
        if (!name || name[0] == '\0')
        {
            continue;
        }
        AudioOutputDevice out{};
        std::strncpy(out.name, name, sizeof(out.name) - 1);
        out.selected = selected == name;
        visit(&out, ud);
    }
    SDL_free(devices);
}

int FFmpegAudioOutput::DesiredChannelsLocked() const
{
    return AudioOutputChannelsForMode(channelMode_);
}

void FFmpegAudioOutput::ApplyGainLocked()
{
    if (!stream_)
    {
        return;
    }
    float gain = muted_ ? 0.0f : static_cast<float>(volume_) / 100.0f;
    if (!muted_ && duckingEnabled_ && ducked_)
    {
        gain *= static_cast<float>(duckingLevel_) / 100.0f;
    }
    SDL_SetAudioStreamGain(stream_, gain);
}

void FFmpegAudioOutput::Flush()
{
    std::lock_guard lock(mutex_);
    if (stream_)
    {
        SDL_ClearAudioStream(stream_);
    }
    lastQueuedPts_ = 0.0;
}

void FFmpegAudioOutput::Close()
{
    std::lock_guard lock(mutex_);
    if (stream_)
    {
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
    }
    if (swr_)
    {
        swr_free(&swr_);
    }
    bytesPerSec_ = 0;
    lastQueuedPts_ = 0.0;
}
