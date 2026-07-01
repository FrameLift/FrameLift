#pragma once

#include <framelift/platform/IMediaPlayer.h>

#include "FFmpegSubtitles.h"
#include "IVideoRenderer.h"
#include "ReadAheadCache.h"
#include "VideoDecodeMode.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

// libav types used only by reference in private method signatures — kept as
// forward declarations so this header (included by App.cpp) stays libav-free.
struct AVCodecContext;
struct AVStream;
struct AVFormatContext;
struct AVFrame;
struct AVBufferRef;

class FFmpegAudioOutput;
class FFmpegPacketQueue;
class FFmpegHwDecode;
class Settings;

// Concrete external FFmpeg + libass implementation of the media playback interface
// family (IMediaPlayback / IMediaProperties / IVideoOutput / IAudioControl /
// ISubtitleControl).
//
// This and FFmpeg* siblings are the ONLY files that may #include <libav*/...> or
// <ass/...> headers.
//
// Per file, a demux thread fans packets into bounded audio/video queues and
// spawns an audio worker (resamples to Qt's raw PCM sink — the master clock) and
// a video worker (swscale → present,
// synced to the clock). A session loop performs keyframe/exact seeks between
// worker spawns, holds the last frame on EOF (still seekable), and handles still
// images / slideshows. Pause, volume/mute and the full host-consumed property set
// are live. Subtitles (libass) and hardware decoding arrive in later phases.
class FFmpegPlayer final : public IMediaPlayback,
                           public IMediaProperties,
                           public IVideoOutput,
                           public IAudioControl,
                           public ISubtitleControl
{
public:
    FFmpegPlayer();
    ~FFmpegPlayer() override;

    FFmpegPlayer(const FFmpegPlayer&) = delete;
    FFmpegPlayer& operator=(const FFmpegPlayer&) = delete;

    void LoadFile(const char* path, double resumePos = 0.0) noexcept override;
    // Abandon the current file and return to the idle state (idle screen, no held
    // frame). Host-only (concrete): used at end-of-playlist so a finished last item
    // doesn't linger as a seekable held frame. No-op when already idle.
    void Stop() noexcept;
    void SetPause(bool paused) noexcept override;
    void TogglePause() noexcept override;
    void ToggleMute() noexcept override;
    void AdjustVolume(int delta) noexcept override;
    void Seek(double seconds) noexcept override;
    void SeekAbsolute(double seconds) noexcept override;
    void SetImageDisplayDuration(double seconds) noexcept override;
    void SetAudioNormalize(bool enabled, const AudioNormalizeParams& params = {}) noexcept override;
    void SetPlaybackOptions(const PlaybackOptions& opts) noexcept override;
    void SetVideoDecodeMode(VideoDecodeMode mode) noexcept;
    void SetReadAheadCache(const ReadAheadCacheOptions& opts) noexcept override;
    void SetSubtitleStyle(const SubtitleStyle& style) noexcept override;
    void SetAudioPreferences(const AudioPreferences& prefs) noexcept override;
    [[nodiscard]] AudioPreferences GetAudioPreferences() const noexcept override;

    // Apply every player-side option from the host Settings in one call (playback,
    // decode mode, read-ahead cache, subtitle style, audio preferences + normalize).
    // Concrete-only: Settings can't cross the playback-interface ABI.
    void ApplySettings(const Settings& s);

    // Briefly duck audio (used on UI notifications); auto-restores after a short
    // timeout, decayed on the audio worker so the host needs no per-frame tick.
    void PulseDucking() noexcept;

    void ToggleSubtitles() noexcept override;
    void CycleSubtitleTrack() noexcept override;

    [[nodiscard]] bool IsMuted() const noexcept override
    {
        return muteEnabled_;
    }

    [[nodiscard]] bool IsNormalizeEnabled() const noexcept override
    {
        return normalizeEnabled_;
    }

    [[nodiscard]] bool IsSubtitlesEnabled() const noexcept override
    {
        return subtitlesEnabled_;
    }

    void AdjustSubtitleDelay(double delta) noexcept override;
    void SetSubtitleDelay(double seconds) noexcept override;

    void EnumerateSubtitleTracks(void (*visit)(const SubtitleTrack*, void*), void* ud) const noexcept override;
    void SelectSubtitleTrack(int64_t id) noexcept override;

    void EnumerateAudioTracks(void (*visit)(const AudioTrack*, void*), void* ud) const noexcept override;
    void SelectAudioTrack(int64_t id) noexcept override;

    void GetDoubleAsync(PlayerProperty prop, void (*cb)(double, bool ok, void*), void* ud) noexcept override;
    void GetInt64Async(PlayerProperty prop, void (*cb)(int64_t, bool ok, void*), void* ud) noexcept override;
    void GetStringAsync(PlayerProperty prop, void (*cb)(const char*, bool ok, void*), void* ud) noexcept override;
    void GetDisplaySizeAsync(void (*cb)(const DisplaySize*, bool ok, void*), void* ud) noexcept override;

    void ObserveProperty(PlayerProperty prop) noexcept override;

    [[nodiscard]] MediaEvent PollEvent() noexcept override;
    void SetWakeupCallback(void (*cb)(void*), void* ud) noexcept override;
    void EnumerateAudioOutputDevices(void (*visit)(const AudioOutputDevice*, void*), void* ud) const noexcept override;

    void InitRender(void* graphicsBackend) noexcept override;
    void ReleaseRender() noexcept;
    void SetRenderUpdateCallback(void (*cb)(void*), void* ud) noexcept override;
    [[nodiscard]] bool HasNewFrame() noexcept override;
    void RenderFrame(int w, int h) noexcept override;
    void PrepareRenderFrame(int w, int h) noexcept;
    void DrawPreparedFrame(int w, int h) noexcept;

private:
    // Background decode/playback thread: waits for a load request, then drives the
    // demux + audio/video workers for the file (PlayFile) until EOF / new load / quit.
    void DecodeThreadMain();
    void PlayFile(const std::string& path, double resumePos);

    // Per-file worker bodies (each on its own thread, spawned by PlayFile).
    void AudioWorker(AVCodecContext* dec, AVStream* stream, double startOffset);
    // hw is non-null when the decoder is armed for hardware decode (downloads frames
    // to system memory before swscale); null / inactive ⇒ unchanged software path.
    void VideoWorker(AVCodecContext* dec, AVStream* stream, int dstW, int dstH, FFmpegHwDecode* hw);
    [[nodiscard]] bool TryEnableHardwareDecode(const AVCodec* codec, AVCodecContext* dec, FFmpegHwDecode& hw);
    // Decodes packets from subQ_ and feeds them to libass (embedded subtitles only).
    void SubtitleWorker(AVCodecContext* dec, AVStream* stream);
    // Reads an external audio container on its own thread, pushing its audio stream
    // into audioQ_ while the main demux loop reads the video container.
    void ExternalAudioDemux(AVFormatContext* fmt, int streamIndex);

    // Master clock (seconds): audio device position when audio exists, else a
    // pausable wall clock derived from the first video frame.
    [[nodiscard]] double GetMasterClock();
    [[nodiscard]] double VideoWallClock();
    [[nodiscard]] double GetSubtitleRenderClock();
    void ClearSubtitleSeekClockOverride();

    // Notify the decode thread / workers parked on cv_, and (Windows) interrupt the
    // video worker's high-resolution frame-pacing wait via videoWakeEvent_.
    void Wake();

    // Queue a MediaEvent for the main thread and fire the wakeup callback.
    void QueueEvent(const MediaEvent& e);
    // Fire the render-update callback (a new video frame is ready to draw).
    void RequestRender();
    // Update idle state and, if observed, emit an IdleActive PropertyChange.
    void SetIdle(bool idle);
    // Recompute core-idle (paused | eof | idle) and emit if observed + changed.
    void UpdateCoreIdle();
    // Emit a PropertyChange for prop iff it is currently observed.
    void EmitFlag(PlayerProperty prop, bool value);
    void EmitDouble(PlayerProperty prop, double value);
    // True when the decode loop should abandon the current file (new load / quit).
    [[nodiscard]] bool StopRequested();
    // Consume the pending seek target (seconds), clearing the request flag.
    [[nodiscard]] double TakePendingSeek();
    // Record an absolute seek target and wake the decode thread / workers.
    void RequestSeek(double target) noexcept;
    // Emit one sparse perf summary for the loaded session; no file paths are logged.
    void EmitPlaybackSummary(const char* reason);

    // ── Track model ──────────────────────────────────────────────────────────
    enum class TrackKind : std::uint8_t
    {
        Audio,
        Subtitle,
    };

    // One selectable audio/subtitle track, embedded in the main container or living
    // in an external sidecar file. id is stable for the lifetime of the loaded file.
    struct TrackEntry
    {
        int64_t id = 0;
        TrackKind kind = TrackKind::Audio;
        int container = 0;    // 0 = main container; >=1 == externalSources_ index + 1
        int streamIndex = -1; // stream index within that container (embedded routing)
        bool external = false;
        bool selected = false;
        std::string label;
        std::string language;
    };

    // A fuzzy-matched sidecar file discovered next to the media (Phase 5 auto-load).
    struct ExternalSource
    {
        std::string path;
        bool isAudio = false; // else subtitle
    };

    // The currently bound audio source. fmt is owned (and closed) only when external;
    // for embedded audio it aliases the main container.
    struct AudioBinding
    {
        AVFormatContext* fmt = nullptr;
        AVCodecContext* dec = nullptr;
        AVStream* stream = nullptr;
        int streamIndex = -1;
        bool external = false;
        double startOffset = 0.0; // subtracted from external-audio pts to match the 0 origin
    };

    // Scan the media directory for fuzzy sidecar subtitle/audio files (gated by the
    // current PlaybackOptions) and populate externalSources_.
    void ScanExternalSources(const std::string& mediaPath);
    // (Re)build tracks_ from the main container's streams + externalSources_ and
    // choose the default selected audio/subtitle. Sets selectedAudioId_/selectedSubId_.
    void BuildTrackList(AVFormatContext* mainFmt, int defaultAudioStream);
    // Refresh the `selected` flags in tracks_ to match selectedAudioId_/selectedSubId_.
    void RefreshSelectedFlags();
    // Tear down `aud` and rebind it to track `id` (embedded or external). Returns true
    // if audio is now available. audioOut_ is reopened for the new stream.
    bool OpenAudioBinding(int64_t id, AVFormatContext* mainFmt, AudioBinding& aud);
    // Apply subtitle selection `id` (-1 == off): open/rebuild the embedded subtitle
    // decoder (out via sDec/sStream/subIdx) or pre-load an external file into libass.
    void OpenSubtitleBinding(
        int64_t id, const std::string& mediaPath, AVFormatContext* mainFmt, int& subIdx, AVCodecContext*& sDec,
        AVStream*& sStream
    );
    // Resolve a track id to its entry (copy) under tracksMutex_; returns false if absent.
    bool FindTrack(int64_t id, TrackEntry& out) const;

    static constexpr std::size_t kPropCount = static_cast<std::size_t>(PlayerProperty::Unknown) + 1;
    static constexpr double kDropThreshold = 0.1; // seconds a frame may lag before being dropped

    struct Callback
    {
        void (*fn)(void*) = nullptr;
        void* ud = nullptr;
    };

    Callback wakeupCb_; // invoked when a MediaEvent is queued (host then PollEvent())
    Callback renderCb_; // invoked when a new video frame is ready (host then RenderFrame())

    std::unique_ptr<IVideoRenderer> renderer_;
    bool rendererReady_ = false;

#if FRAMELIFT_MODULE_GRAPHICS_VULKAN
    // Zero-copy Vulkan decode (#18). vkHwDevice_ is an AV_HWDEVICE_TYPE_VULKAN context
    // WRAPPING the renderer's Vulkan device, built once in InitRender and reused per
    // file; null on non-Vulkan backends / when video-decode is unsupported (then the
    // CPU-RGBA8 path runs). vulkanZeroCopyAvailable_ gates per-file selection in PlayFile.
    AVBufferRef* vkHwDevice_ = nullptr;
    bool vulkanZeroCopyAvailable_ = false;
    // FFmpeg's Vulkan video-decode backend faults (VK_ERROR_DEVICE_LOST) on the NVIDIA
    // driver, so Auto mode must not auto-select it there — NVDEC/CUDA is NVIDIA's reliable
    // path and comes next in AutoVideoDecodePreference(). Explicit VulkanZeroCopy still tries.
    bool vulkanAdapterIsNvidia_ = false;
#endif

    std::thread decodeThread_;
    std::mutex mutex_; // guards the command/event state below + drives cv_
    std::condition_variable cv_;
    std::atomic<bool> shutdown_{false};
    // Win32 auto-reset event (HANDLE; void* keeps <windows.h> out of this header) the
    // video worker waits on alongside its frame-pacing timer; signalled by Wake().
    void* videoWakeEvent_ = nullptr;

    // Audio output + per-file packet queues (forward-declared; defined in the .cpp
    // so libav stays out of this header).
    // Shared memory-bounded read-ahead budget + hit/miss metrics for the three
    // queues. Declared before the queues so it outlives them: their
    // destructors Flush() and touch this via their budget_ pointer.
    ReadAheadCache cache_;
    std::unique_ptr<FFmpegAudioOutput> audioOut_;
    std::unique_ptr<FFmpegPacketQueue> audioQ_;
    std::unique_ptr<FFmpegPacketQueue> videoQ_;
    std::unique_ptr<FFmpegPacketQueue> subQ_;
    std::thread audioThread_;
    std::thread videoThread_;
    std::thread subtitleThread_;
    std::thread extAudioThread_; // external-audio container demux

    // libass subtitle pipeline (owns the active ASS track; shared between the
    // subtitle worker and the render thread, internally locked).
    std::unique_ptr<FFmpegSubtitles> subtitles_;

    // Load command + current file, guarded by mutex_.
    std::string pendingPath_;
    double pendingResume_ = 0.0;
    bool hasPendingLoad_ = false;
    // Set by Stop() to abandon the current file and return to idle without loading a
    // new one (StopRequested() folds it in). Lingers harmlessly while the decode thread
    // is parked; cleared when the next load is consumed.
    bool stopRequested_ = false;
    std::string currentPath_;

    // Events queued for the main thread to drain via PollEvent(). Guarded by mutex_.
    std::queue<MediaEvent> events_;

    // Video frame handoff (decode thread → render thread). Three buffers cycle
    // through swap under frameMutex_: decode-owned → pending → display-owned.
    std::mutex frameMutex_;
    std::vector<uint8_t> pendingPixels_; // newest RGBA frame awaiting display
    std::vector<uint8_t> displayPixels_; // frame owned by the render thread
    int pendingW_ = 0;
    int pendingH_ = 0;
    bool pendingValid_ = false;
    std::atomic<bool> newFramePending_{false};
#if FRAMELIFT_MODULE_GRAPHICS_VULKAN
    // Zero-copy path: instead of the RGBA buffers, the decode thread hands a ref'd
    // AVFrame (carrying an AVVkFrame) to the render thread. pendingVkFrame_ is guarded by
    // frameMutex_; displayVkFrame_ is render-thread-owned. pendingIsVulkan_ distinguishes
    // which pending channel is live (a file is one path or the other for its lifetime).
    AVFrame* pendingVkFrame_ = nullptr;
    AVFrame* displayVkFrame_ = nullptr;
    bool pendingIsVulkan_ = false;
    bool displayIsVulkan_ = false; // render-thread-owned: last frame handed to the renderer
#endif
    bool preparedOverlayActive_ = false;

    // Observable / queryable state.
    std::atomic<int64_t> displayWidth_{0};
    std::atomic<int64_t> displayHeight_{0};
    std::atomic<int64_t> droppedFrames_{0};
    std::atomic<int64_t> mistimedFrames_{0}; // frames presented while already behind the master clock
    std::atomic<int64_t> decodeErrors_{0};   // packets that failed avcodec_send_packet
    std::atomic<bool> idle_{true};
    std::atomic<bool> paused_{false};
    std::atomic<bool> eofReached_{false};
    std::atomic<bool> coreIdle_{true};
    std::atomic<double> duration_{0.0};
    double speed_ = 1.0;    // read-only: no setter exists in IMediaPlayer (yet)
    int volume_ = 100;      // canonical 0–100 volume, mirrored to the audio device
    bool hasVideo_ = false; // set by PlayFile; audio worker drives TimePos when false

    // Seeking. seekTarget_/hasPendingSeek_ are a request from the main thread to
    // the decode thread (guarded by mutex_). seekSkipPts_ is set by the decode
    // thread before spawning workers (read-only to them) — exact seeks drop frames
    // before it. seekRefresh_ lets the video worker present one frame while paused.
    bool hasPendingSeek_ = false;
    double seekTarget_ = 0.0;
    // Held-key seek state (all guarded by mutex_). Two distinct release points, so two
    // flags — conflating them re-introduces the jump-to-0 on files with audio:
    //  • seekSettled_  — a post-seek *frame has been painted*. Gates the held-step
    //    pipeline: the decode loop / workers must show the current seek's frame before
    //    honouring the next pending seek, so a held key steps visibly instead of freezing.
    //    Set at the video present (audio deliver for audio-only files).
    //  • seekClockValid_ — the *authoritative master clock* is re-established (audio clock
    //    when a device is open, else the video wall clock). Gates the relative-seek anchor
    //    in Seek(): until true, GetMasterClock() reads ~0, so a repeat must accumulate
    //    against seekTarget_ instead of re-targeting from the start. The video frame can
    //    paint before the audio worker re-feeds, so this must NOT key off the video frame.
    bool seekSettled_ = true;
    bool seekClockValid_ = true;
    double seekSkipPts_ = -1e18;
    std::atomic<bool> seekRefresh_{false};
    std::atomic<bool> hrSeek_{true}; // PlaybackOptions.hrSeek (exact vs keyframe)
    std::atomic<bool> hwdec_{true};  // PlaybackOptions.hwdec (try hardware decode on load)
    std::atomic<VideoDecodeMode> videoDecodeMode_{VideoDecodeMode::Auto};
    std::atomic<double> imageDisplayDuration_{0.0}; // <= 0 ⇒ hold a still image indefinitely
    std::string mediaTitle_;                        // metadata title (guarded by mutex_)
    std::string hwDecName_ = "no";                  // active hw decoder name / "no" (guarded by mutex_)

    // Which properties the host/plugins have subscribed to (indexed by enum value).
    std::array<std::atomic<bool>, kPropCount> observed_{};

    // Video-only wall clock baseline (guarded by mutex_): maps a pts to a wall time
    // so the fallback clock can be frozen across pause.
    double videoClockPts_ = 0.0;
    bool videoClockSet_ = false;
    std::chrono::steady_clock::time_point videoClockWall_;
    std::chrono::steady_clock::time_point pauseWall_;

    // Toggle state mirrors (defaults match the player's initial state).
    bool muteEnabled_ = false;
    std::atomic<bool> normalizeEnabled_{false}; // also read by the audio worker
    bool subtitlesEnabled_ = true;

    // Audio-normalization filter state. normalizeParams_ is guarded by mutex_; the audio
    // worker watches normalizeGen_ (bumped on every SetAudioNormalize) to (re)build its
    // libavfilter graph in place — no seek needed.
    AudioNormalizeParams normalizeParams_;
    std::atomic<uint64_t> normalizeGen_{0};

    // PlaybackOptions auto-load flags (consulted by ScanExternalSources at load).
    bool subAutoLoad_ = true;
    bool audioFileAutoLoad_ = true;

    // ── Track model (snapshot for the main-thread Enumerate*/Select* calls) ────
    // tracksMutex_ is distinct from mutex_ and is only ever held briefly (no cv_
    // waits under it); never acquire mutex_ while holding it.
    mutable std::mutex tracksMutex_;
    std::vector<TrackEntry> tracks_;
    int64_t selectedAudioId_ = -1;
    int64_t selectedSubId_ = -1; // -1 == subtitles off / none
    int64_t nextTrackId_ = 1;
    std::vector<ExternalSource> externalSources_; // discovered sidecar files for this load

    // Pending track switches: a Select* on the main thread records the request and
    // forces a seek-to-current; the decode thread rebuilds the binding at the seek
    // boundary. Guarded by mutex_.
    int64_t pendingAudioId_ = -1;
    int64_t pendingSubId_ = -1;
    bool hasPendingAudioSwitch_ = false;
    bool hasPendingSubSwitch_ = false;

    // Subtitle rendering state.
    std::atomic<double> subtitleDelay_{0.0};       // seconds; positive delays subtitles
    std::vector<unsigned char> overlayScratch_;    // render-thread-owned overlay pixels
    bool overlayActive_ = false;                   // render-thread-owned: draw overlay this frame
    bool subtitleSeekClockOverrideActive_ = false; // guarded by mutex_
    double subtitleSeekClockOverride_ = 0.0;       // guarded by mutex_

    // User subtitle preferences. Styling is forwarded to libass on change; the
    // behavior fields (preferredLang/preferForced) drive BuildTrackList. Guarded by
    // tracksMutex_ for the behavior fields read on the decode thread.
    SubtitleStyle subtitleStyle_{};
    AudioPreferences audioPrefs_{};
    std::atomic<int> audioSyncOffsetMs_{0};
};
