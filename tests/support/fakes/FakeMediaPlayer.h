#pragma once

#include <framelift/platform/IMediaPlayer.h>

#include <string>

// Minimal hand-written fake implementing the whole media playback interface family.
// Records the calls the tests assert on (LoadFile / SetPause / SetImageDisplayDuration);
// everything else is a no-op. Register it under whichever facet a test exercises, e.g.
// ctx.RegisterService<IMediaPlayback>(&fake).
class FakeMediaPlayer final : public IMediaPlayback,
                              public IMediaProperties,
                              public IVideoOutput,
                              public IAudioControl,
                              public ISubtitleControl
{
public:
    // ── Recorded state the tests inspect ──────────────────────────────────────
    std::string loadedPath;
    double loadedResumePos = -1.0;
    int loadCount = 0;
    bool pauseSet = true; // last SetPause() arg
    double imageDuration = -1.0;
    AudioPreferences audioPrefs_{}; // last SetAudioPreferences() arg, returned by GetAudioPreferences()

    // ── Playback commands ──────────────────────────────────────────────────────
    void LoadFile(const char* path, double resumePos) noexcept override
    {
        loadedPath = path ? path : "";
        loadedResumePos = resumePos;
        ++loadCount;
    }

    void SetPause(bool paused) noexcept override
    {
        pauseSet = paused;
    }

    void TogglePause() noexcept override
    {
    }

    void ToggleMute() noexcept override
    {
    }

    void AdjustVolume(int) noexcept override
    {
    }

    void Seek(double) noexcept override
    {
    }

    void SeekAbsolute(double) noexcept override
    {
    }

    void SetImageDisplayDuration(double seconds) noexcept override
    {
        imageDuration = seconds;
    }

    void SetAudioNormalize(bool, const AudioNormalizeParams&) noexcept override
    {
    }

    void SetPlaybackOptions(const PlaybackOptions&) noexcept override
    {
    }

    void SetReadAheadCache(const ReadAheadCacheOptions&) noexcept override
    {
    }

    void SetSubtitleStyle(const SubtitleStyle&) noexcept override
    {
    }

    void SetAudioPreferences(const AudioPreferences& prefs) noexcept override
    {
        audioPrefs_ = prefs;
    }

    [[nodiscard]] AudioPreferences GetAudioPreferences() const noexcept override
    {
        return audioPrefs_;
    }

    // ── Tracks ─────────────────────────────────────────────────────────────────
    void EnumerateSubtitleTracks(void (*)(const SubtitleTrack*, void*), void*) const noexcept override
    {
    }

    void SelectSubtitleTrack(int64_t) noexcept override
    {
    }

    void EnumerateAudioTracks(void (*)(const AudioTrack*, void*), void*) const noexcept override
    {
    }

    void SelectAudioTrack(int64_t) noexcept override
    {
    }

    void ToggleSubtitles() noexcept override
    {
    }

    // ── Toggle state ───────────────────────────────────────────────────────────
    [[nodiscard]] bool IsMuted() const noexcept override
    {
        return false;
    }

    [[nodiscard]] bool IsNormalizeEnabled() const noexcept override
    {
        return false;
    }

    [[nodiscard]] bool IsSubtitlesEnabled() const noexcept override
    {
        return false;
    }

    void CycleSubtitleTrack() noexcept override
    {
    }

    void AdjustSubtitleDelay(double) noexcept override
    {
    }

    void SetSubtitleDelay(double) noexcept override
    {
    }

    // ── Async queries ──────────────────────────────────────────────────────────
    void GetDoubleAsync(PlayerProperty, void (*)(double, bool, void*), void*) noexcept override
    {
    }

    void GetInt64Async(PlayerProperty, void (*)(int64_t, bool, void*), void*) noexcept override
    {
    }

    void GetStringAsync(PlayerProperty, void (*)(const char*, bool, void*), void*) noexcept override
    {
    }

    void GetDisplaySizeAsync(void (*)(const DisplaySize*, bool, void*), void*) noexcept override
    {
    }

    void ObserveProperty(PlayerProperty) noexcept override
    {
    }

    // ── Events / render ────────────────────────────────────────────────────────
    [[nodiscard]] MediaEvent PollEvent() noexcept override
    {
        return {};
    }

    void SetWakeupCallback(void (*)(void*), void*) noexcept override
    {
    }

    void EnumerateAudioOutputDevices(void (*)(const AudioOutputDevice*, void*), void*) const noexcept override
    {
    }

    void InitRender(void*) noexcept override
    {
    }

    void SetRenderUpdateCallback(void (*)(void*), void*) noexcept override
    {
    }

    [[nodiscard]] bool HasNewFrame() noexcept override
    {
        return false;
    }

    void RenderFrame(int, int) noexcept override
    {
    }
};
