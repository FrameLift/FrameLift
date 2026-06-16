#pragma once

#include <cstdint>

// ── Event types ───────────────────────────────────────────────────────────────

enum class MediaEventType : std::uint8_t
{
    None,            // no event pending – stop polling
    Other,           // unrecognised event – safe to ignore
    EndFile,         // file finished / stopped
    VideoReconfig,   // video resolution / format changed
    PropertyChange,  // an observed property changed
    StartFile,       // a new file started loading
    FileLoaded,      // file loaded; tracks/metadata ready
    PlaybackRestart, // playback (re)started after a seek
    Seek,            // a seek was initiated
    AudioReconfig,   // audio output reconfigured
};

enum class EndFileReason : std::uint8_t
{
    Eof,
    Error,
    Other
};

enum class PropertyType : std::uint8_t
{
    Flag,
    Double,
    Int64,
    String
};

// Every player property the application cares about.
// Using an enum instead of strings eliminates typos and makes exhaustiveness
// checking possible. Add new values here when new properties are needed.
enum class PlayerProperty : std::uint8_t
{
    // ── Observed (pushed via PropertyChange events) ───────────────────────────
    IdleActive, // "idle-active"  Flag   – true when no file is loaded
    TimePos,    // "time-pos"     Double – current playback position (seconds)
    Duration,   // "duration"     Double – media duration (seconds)
    Pause,      // "pause"        Flag   – true when playback is paused

    // ── Polled (read on demand via GetDoubleAsync / GetInt64Async) ────────────
    DisplayWidth,  // "dwidth"                Int64  – decoded video display width  (pixels)
    DisplayHeight, // "dheight"               Int64  – decoded video display height (pixels)
    Volume,        // "volume"                Double – current playback volume (0-100)

    // ── Polled (debug stats) ──────────────────────────────────────────────────
    Path,           // "path"                  String – current file path
    MediaTitle,     // "media-title"           String – display title
    HwDecCurrent,   // "hwdec-current"         String – active hardware decoder name
    DroppedFrames,  // "frame-drop-count"      Int64  – number of dropped frames
    MistimedFrames, // "mistimed-frame-count"  Int64  – number of mistimed frames
    CacheUsed,      // "cache-used"            Int64  – read-ahead cache currently used (KB)

    // ── Additional observable state (pushed via PropertyChange events) ────────
    Mute,           // "mute"            Flag   – true when audio is muted
    CoreIdle,       // "core-idle"       Flag   – true when nothing is being decoded
    Seeking,        // "seeking"         Flag   – true while a seek is in progress
    PausedForCache, // "paused-for-cache" Flag  – true when stalled waiting for the cache
    EofReached,     // "eof-reached"     Flag   – true when the end of the file was reached
    Speed,          // "speed"           Double – current playback speed multiplier
    PercentPos,     // "percent-pos"     Double – playback position as a percentage (0-100)

    // ── Read-ahead cache metrics (appended before Unknown to keep the
    //    ABI ordinals above stable). Polled via GetInt64Async. ───────────────────
    CacheHits,   // "cache-hit-count"   Int64 – packets served without a read-ahead stall
    CacheMisses, // "cache-miss-count"  Int64 – read-ahead underruns (decode worker had to wait)

    Unknown, // placeholder for unrecognised properties – safe to ignore
};

struct PropertyEvent
{
    PlayerProperty prop = PlayerProperty::Unknown; // which property changed
    PropertyType type = PropertyType::Flag;        // active member of the value union

    union Value {
        int flag;
        double dbl;
        int64_t i64;
        char str[256]; // active when type == String – NUL-terminated, value is copied
    } value{};
};

struct MediaEvent
{
    MediaEventType type = MediaEventType::None;
    EndFileReason endReason = EndFileReason::Other;
    PropertyEvent property;
};

// ── IMediaPlayer ──────────────────────────────────────────────────────────────

// A single subtitle track reported by the player.
struct SubtitleTrack
{
    int64_t id = 0;        // track id — use with SelectSubtitleTrack()
    char label[256] = {};  // human-readable name (title, language, or "Track N"), NUL-terminated
    bool selected = false; // true when this is the currently active track
};

// A single audio track reported by the player.
struct AudioTrack
{
    int64_t id = 0;        // track id — use with SelectAudioTrack()
    char label[256] = {};  // human-readable name (title, language, or "Track N"), NUL-terminated
    bool selected = false; // true when this is the currently active track
};

// Decoded video dimensions in pixels.
struct DisplaySize
{
    int64_t width;
    int64_t height;
};

// Parameters for the dynamic audio normalization filter chain (dynaudnorm + asoftclip).
// Defaults match the filter's tuned values; all fields map 1-to-1 to libavfilter options.
struct AudioNormalizeParams
{
    int frameLen = 100;  // dynaudnorm f: analysis frame length in ms
    int gaussSize = 5;   // dynaudnorm g: gaussian smoothing window in frames (must be odd)
    float peak = 0.95f;  // dynaudnorm p: target peak level (0.0–1.0)
    float maxGain = 5.f; // dynaudnorm m: maximum gain factor per frame
    float volume = 1.5f; // volume filter: output gain multiplier applied after normalization
};

// User-configurable playback options.
struct PlaybackOptions
{
    bool hwdec = true;             // try hardware video decode (falls back to software)
    bool hrSeek = true;            // exact-frame seeking vs. nearest-keyframe seeking
    bool subAutoLoad = true;       // auto-load sidecar subtitle files matching the media
    bool audioFileAutoLoad = true; // auto-load sidecar audio files matching the media
};

// Read-ahead (demuxer) cache options. The cache bounds how far the
// demuxer prefetches ahead of playback by total buffered packet bytes, shared
// across the audio/video/subtitle streams.
struct ReadAheadCacheOptions
{
    bool enabled = true;         // false ⇒ fall back to the per-stream packet-count bound
    int64_t maxBytes = 64 << 20; // memory budget in bytes (default 64 MiB)
};

// Pure interface for a media playback backend.
// All backend-specific headers (FFmpeg, libass, etc.) are kept behind this boundary.
class IMediaPlayer
{
public:
    static constexpr const char* InterfaceId = "framelift.IMediaPlayer";
    virtual ~IMediaPlayer() = default;

    // ── Playback commands ──────────────────────────────────────────────────────
    // Load a file and begin playing. Pass resumePos > 0 to resume from that
    // position (seconds); pass 0.0 to start from the beginning.
    virtual void LoadFile(const char* path, double resumePos = 0.0) noexcept = 0;

    virtual void SetPause(bool paused) noexcept = 0;
    virtual void TogglePause() noexcept = 0;
    virtual void ToggleMute() noexcept = 0;
    virtual void AdjustVolume(int delta) noexcept = 0;
    virtual void Seek(double seconds) noexcept = 0;
    virtual void SeekAbsolute(double seconds) noexcept = 0;
    virtual void SetImageDisplayDuration(double seconds) noexcept = 0;
    virtual void SetAudioNormalize(bool enabled, const AudioNormalizeParams& params = {}) noexcept = 0;
    virtual void SetPlaybackOptions(const PlaybackOptions& opts) noexcept = 0;

    // ── Subtitle / audio tracks ────────────────────────────────────────────────
    // Enumerate tracks: visit(track, ud) is called once per track.
    virtual void EnumerateSubtitleTracks(
        void (*visit)(const SubtitleTrack* track, void* ud), void* ud
    ) const noexcept = 0;
    virtual void SelectSubtitleTrack(int64_t id) noexcept = 0;
    virtual void EnumerateAudioTracks(void (*visit)(const AudioTrack* track, void* ud), void* ud) const noexcept = 0;
    virtual void SelectAudioTrack(int64_t id) noexcept = 0;
    virtual void ToggleSubtitles() noexcept = 0;

    // ── Toggle state ──────────────────────────────────────────────────────────
    [[nodiscard]] virtual bool IsMuted() const noexcept = 0;
    [[nodiscard]] virtual bool IsNormalizeEnabled() const noexcept = 0;
    [[nodiscard]] virtual bool IsSubtitlesEnabled() const noexcept = 0;
    virtual void CycleSubtitleTrack() noexcept = 0;
    virtual void AdjustSubtitleDelay(double delta) noexcept = 0;
    virtual void SetSubtitleDelay(double seconds) noexcept = 0;

    // ── Async property queries ─────────────────────────────────────────────────
    // Callbacks are invoked on the main thread from PollEvent().
    // ok=false when the property is unavailable or the player is idle.
    // cb/ud must remain valid until the callback fires (typically one or two frames).
    virtual void GetDoubleAsync(
        PlayerProperty prop, void (*cb)(double value, bool ok, void* ud), void* ud
    ) noexcept = 0;
    virtual void GetInt64Async(
        PlayerProperty prop, void (*cb)(int64_t value, bool ok, void* ud), void* ud
    ) noexcept = 0;
    // value is a NUL-terminated string valid only for the callback duration.
    virtual void GetStringAsync(
        PlayerProperty prop, void (*cb)(const char* value, bool ok, void* ud), void* ud
    ) noexcept = 0;
    // Fetches display width + height in parallel; fires once both are ready.
    virtual void GetDisplaySizeAsync(void (*cb)(const DisplaySize* size, bool ok, void* ud), void* ud) noexcept = 0;

    virtual void ObserveProperty(PlayerProperty prop) noexcept = 0;

    // ── Events ────────────────────────────────────────────────────────────────
    [[nodiscard]] virtual MediaEvent PollEvent() noexcept = 0;

    // Background-thread callbacks — invoked from the player's internal thread.
    // cb/ud must remain valid for the player's lifetime.
    virtual void SetWakeupCallback(void (*cb)(void* ud), void* ud) noexcept = 0;

    // ── Rendering ─────────────────────────────────────────────────────────────
    // graphicsBackend: opaque handle to the host's active graphics backend (a
    // host-internal IGraphicsBackend*). The player builds its video renderer from it,
    // so the same call works for the OpenGL and Vulkan backends.
    virtual void InitRender(void* graphicsBackend) noexcept = 0;
    virtual void SetRenderUpdateCallback(void (*cb)(void* ud), void* ud) noexcept = 0;
    [[nodiscard]] virtual bool HasNewFrame() noexcept = 0;
    virtual void RenderFrame(int w, int h) noexcept = 0;

    // ── Read-ahead cache ───────────────────────────────────────────
    // Configure the memory-bounded demuxer read-ahead cache. Applied on the next
    // LoadFile(). Appended at the end of the interface to keep the vtable layout
    // of earlier slots stable (host-provided surface; MINOR ABI addition).
    virtual void SetReadAheadCache(const ReadAheadCacheOptions& opts) noexcept = 0;
};