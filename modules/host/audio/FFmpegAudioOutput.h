#pragma once

extern "C"
{
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
}

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <framelift/platform/IMediaPlayer.h>

struct AVFrame;
struct SwrContext;
struct AudioSink; // Qt PCM backend (QAudioSink + ring buffer + audio thread); defined in the .cpp

// Audio output for the FFmpeg backend (issue #8, Phase 3): resamples decoded
// audio to the device format and feeds a playback stream, while exposing the
// playback position as the master clock that video is synced against.
//
// Qt6 PCM backend: a QAudioSink driven in PULL mode from a thread-safe ring buffer.
// Qt Multimedia is used only for raw audio output/device enumeration here; demux,
// decode, filtering, subtitles, and video presentation stay in FFmpegPlayer and the
// external FFmpeg/libass stack. The QAudioSink lives on its OWN QThread with an event
// loop (it needs one to pump audio, which the FFmpeg worker threads don't have), so
// device lifecycle/gain/suspend are marshalled onto that thread; Feed() (audio worker)
// and MasterClock()/QueuedBytes() (video/other workers) only touch the lock-protected
// ring + atomics, never Qt playback classes.
//
// Lifetime: Open()/Close() run on the decode thread with no decode workers live;
// the audio worker calls Feed() and the video worker calls MasterClock() while a
// file plays; the host's main thread calls the SetPaused/SetVolume/SetMute and
// Flush controls at any time. A single mutex serialises all device access so the
// control calls are safe against the file-boundary Open()/Close().
class FFmpegAudioOutput
{
public:
    FFmpegAudioOutput();
    ~FFmpegAudioOutput();

    FFmpegAudioOutput(const FFmpegAudioOutput&) = delete;
    FFmpegAudioOutput& operator=(const FFmpegAudioOutput&) = delete;

    // Open the default playback device plus a resampler from the decoded format to
    // F32 / stereo / srcRate. Closes any previously open device first. Returns
    // false on failure (the caller then plays video-only / silent).
    bool Open(int srcRate, const AVChannelLayout& srcLayout, AVSampleFormat srcFmt);

    // Resample one decoded frame and queue it for playback; the audio clock then
    // advances to the end of this frame. ptsSec is the frame's start timestamp in
    // seconds. No-op when not open.
    void Feed(const AVFrame* frame, double ptsSec);

    // Timestamp (seconds) of the audio currently audible — the master clock.
    [[nodiscard]] double MasterClock() const;
    // Resampled bytes still queued in the device (used to throttle the worker).
    [[nodiscard]] int64_t QueuedBytes() const;

    [[nodiscard]] bool HasDevice() const;

    [[nodiscard]] int BytesPerSec() const
    {
        return bytesPerSec_;
    }

    void SetPaused(bool paused);
    void SetVolume(int volume0to100); // applied as device gain unless muted
    void SetMute(bool muted);
    void SetPreferences(const AudioPreferences& prefs);
    // Duck the output now and schedule auto-restore after a short timeout. The
    // decay runs inside Feed() on the audio worker, so no external tick is needed.
    void PulseDuck();
    void EnumerateDevices(void (*visit)(const AudioOutputDevice* device, void* ud), void* ud) const;

    void Flush(); // drop queued audio and reset the clock baseline (seek / restart)
    void Close();

private:
    void CloseLocked();     // tear down sink + resampler (mutex_ held)
    void ApplyGainLocked(); // push volume_/muted_ to the device (mutex_ held)
    [[nodiscard]] int DesiredChannelsLocked() const;
    [[nodiscard]] float CurrentGainLocked() const;   // effective gain from volume/mute/duck
    [[nodiscard]] int64_t QueuedBytesLocked() const; // unheard bytes (ring + sink buffer)

    mutable std::mutex mutex_;        // serialises swr + clock + sink lifecycle access
    std::unique_ptr<AudioSink> sink_; // Qt QAudioSink PCM backend (own thread + ring buffer)
    SwrContext* swr_ = nullptr;
    std::vector<uint8_t> buffer_; // scratch for one resampled chunk (Feed only)

    int bytesPerSec_ = 0; // device bytes/sec = freq * channels * sizeof(float)
    int dstChannels_ = 2;
    int dstRate_ = 0;
    double lastQueuedPts_ = 0.0; // pts (s) at the END of the last queued chunk

    int volume_ = 100; // 0–100, mirrors the player's canonical value
    bool muted_ = false;
    bool paused_ = false; // last SetPaused value; applied to sinks created while paused
    std::string preferredDevice_;
    std::string sinkDevice_; // device the live sink_ was opened with (for reuse checks)
    AudioChannelMode channelMode_ = AudioChannelMode::Auto;
    bool duckingEnabled_ = false;
    int duckingLevel_ = 50;
    bool ducked_ = false;
    std::chrono::steady_clock::time_point duckUntil_{}; // auto-restore deadline while ducked_
};
