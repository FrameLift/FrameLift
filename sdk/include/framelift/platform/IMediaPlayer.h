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
    Notice,          // non-fatal informational notice (playback continues); kind in property.value.i64
};

enum class EndFileReason : std::uint8_t
{
    Eof,
    Error,
    Other,
    // Classified failure reasons.
    NotFound,    // file/URL missing or inaccessible
    Unsupported, // no demuxer/decoder/protocol for this container or codec
    Corrupt,     // invalid or truncated data
    NoStream,    // opened, but no playable audio or video stream
};

// Payload for a MediaEventType::Notice, carried in MediaEvent.property.value.i64. These
// flag a stream that was dropped because its decoder was unavailable, while the file
// still plays the other stream — the host turns each into a user notification.
enum class MediaNoticeKind : std::int64_t
{
    VideoUnsupported, // video decoder unavailable; playing audio only
    AudioUnsupported, // audio decoder unavailable; playing video only
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

    DecodeErrors, // "decode-error-count" Int64 – packets that failed to decode (send_packet errors)

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

// A playback output device reported by the platform backend.
struct AudioOutputDevice
{
    char name[256] = {};    // stable user-facing platform device name; empty means system default
    bool isDefault = false; // true for the synthetic "System default" entry
    bool selected = false;  // true when this is the active/preferred device
};

enum class AudioChannelMode : int
{
    Auto = 0,     // backend default, currently stereo fallback
    Mono = 1,     // force mono output
    Stereo = 2,   // force stereo output
    Surround = 3, // prefer 5.1 output, fallback to stereo when unavailable
};

enum class AudioDuckingTrigger : int
{
    Notifications = 0, // app-owned transient notifications / UI sounds
};

// User-configurable audio behavior. Pushed into the player on settings change;
// device/channel/default-volume take effect for the active stream by reopening
// audio output when needed, while track language applies on the next LoadFile().
struct AudioPreferences
{
    char preferredLang[8] = {};  // ISO 639 audio language to auto-select; empty = file/default
    char outputDevice[256] = {}; // platform playback device name; empty = system default
    int defaultVolume = 100;     // 0-100
    int syncOffsetMs = 0;        // positive delays audio relative to video
    AudioChannelMode channelMode = AudioChannelMode::Auto;
    bool duckingEnabled = false;
    int duckingLevel = 50; // playback gain while ducked, percent of current volume
    AudioDuckingTrigger duckingTrigger = AudioDuckingTrigger::Notifications;
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

// Subtitle edge (border) style.
enum class SubtitleEdgeStyle : int
{
    None = 0,       // no outline or shadow
    Outline = 1,    // outline around the glyphs
    DropShadow = 2, // outline + drop shadow
    OpaqueBox = 3,  // text drawn over an opaque box (BackColour)
};

// User-configurable subtitle appearance + track-selection behavior. Pushed into the
// player on settings change; the player forwards styling to libass (the override is
// applied to the active track on top of its embedded style — mpv's "ass-override"
// model) and uses the behavior fields to pick the initial subtitle track.
//
// Colours are packed as ASS 0xRRGGBBAA where the AA byte is *transparency*
// (0x00 = fully opaque, 0xFF = fully transparent), matching libass' convention.
struct SubtitleStyle
{
    bool overrideEnabled = false; // master switch — when false the file's own style is used verbatim

    // ── Appearance ───────────────────────────────────────────────────────────
    float fontScale = 1.0f;              // global font-size multiplier (1.0 = file default)
    char fontFamily[128] = {};           // override font family; empty = keep the file's font
    uint32_t textColor = 0x00000000u;    // primary fill colour (0xRRGGBBAA)
    uint32_t outlineColor = 0x00000000u; // outline/border colour
    uint32_t backColor = 0x00000080u;    // shadow / opaque-box colour (alpha = transparency)
    SubtitleEdgeStyle edgeStyle = SubtitleEdgeStyle::Outline;
    float outlineWidth = 2.0f;  // outline thickness in pixels
    float shadowDepth = 0.0f;   // drop-shadow offset in pixels
    int alignment = 0;          // numpad 1-9 (\an); 0 = keep the file's alignment
    float lineSpacing = 0.0f;   // extra space between lines, pixels
    float letterSpacing = 0.0f; // extra space between glyphs, pixels

    // ── Track-selection behavior ─────────────────────────────────────────────
    char preferredLang[8] = {}; // ISO 639 language to auto-select; empty = no preference
    bool preferForced = false;  // prefer a "forced" subtitle track when present
};

// The media playback backend is exposed to plugins as a family of small,
// independently-discovered capability interfaces rather than one god-interface.
// One host object (FFmpegPlayer) implements them all and registers under each id;
// a consumer fetches only the facets it uses via ctx.GetService<T>() and degrades
// gracefully when a facet returns nullptr. Adding a new playback capability is a
// NEW interface here — never an append to an existing one — so the ABI version and
// the vtable layout of every interface below stay frozen.
//
// All backend-specific headers (FFmpeg, libass, etc.) are kept behind this boundary.

// Transport: loading, play/pause, seeking, image timing, options + the event pump.
class IMediaPlayback
{
public:
    static constexpr const char* InterfaceId = "framelift.IMediaPlayback";
    virtual ~IMediaPlayback() = default;

    // Load a file and begin playing. Pass resumePos > 0 to resume from that
    // position (seconds); pass 0.0 to start from the beginning.
    virtual void LoadFile(const char* path, double resumePos = 0.0) noexcept = 0;

    virtual void SetPause(bool paused) noexcept = 0;
    virtual void TogglePause() noexcept = 0;
    virtual void Seek(double seconds) noexcept = 0;
    virtual void SeekAbsolute(double seconds) noexcept = 0;
    virtual void SetImageDisplayDuration(double seconds) noexcept = 0;
    virtual void SetPlaybackOptions(const PlaybackOptions& opts) noexcept = 0;

    // Configure the memory-bounded demuxer read-ahead cache. Applied on the next LoadFile().
    virtual void SetReadAheadCache(const ReadAheadCacheOptions& opts) noexcept = 0;

    // Drain one pending backend event. Returns type==None when the queue is empty.
    [[nodiscard]] virtual MediaEvent PollEvent() noexcept = 0;

    // Background-thread callback — invoked from the player's internal thread to nudge
    // the host to PollEvent(). cb/ud must remain valid for the player's lifetime.
    virtual void SetWakeupCallback(void (*cb)(void* ud), void* ud) noexcept = 0;
};

// Async property queries. Callbacks are invoked on the main thread from PollEvent().
// ok=false when the property is unavailable or the player is idle. cb/ud must remain
// valid until the callback fires (typically one or two frames).
class IMediaProperties
{
public:
    static constexpr const char* InterfaceId = "framelift.IMediaProperties";
    virtual ~IMediaProperties() = default;

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
};

// Video output / presentation surface.
class IVideoOutput
{
public:
    static constexpr const char* InterfaceId = "framelift.IVideoOutput";
    virtual ~IVideoOutput() = default;

    // graphicsBackend: opaque handle to the host's active graphics backend (a
    // host-internal IGraphicsBackend*). The player builds its video renderer from it,
    // so the same call works for the OpenGL and Vulkan backends.
    virtual void InitRender(void* graphicsBackend) noexcept = 0;
    virtual void SetRenderUpdateCallback(void (*cb)(void* ud), void* ud) noexcept = 0;
    [[nodiscard]] virtual bool HasNewFrame() noexcept = 0;
    virtual void RenderFrame(int w, int h) noexcept = 0;
};

// Audio: mute/volume, normalization, track selection, output device + preferences.
class IAudioControl
{
public:
    static constexpr const char* InterfaceId = "framelift.IAudioControl";
    virtual ~IAudioControl() = default;

    virtual void ToggleMute() noexcept = 0;
    virtual void AdjustVolume(int delta) noexcept = 0;
    [[nodiscard]] virtual bool IsMuted() const noexcept = 0;

    virtual void SetAudioNormalize(bool enabled, const AudioNormalizeParams& params = {}) noexcept = 0;
    [[nodiscard]] virtual bool IsNormalizeEnabled() const noexcept = 0;

    virtual void EnumerateAudioTracks(void (*visit)(const AudioTrack* track, void* ud), void* ud) const noexcept = 0;
    virtual void SelectAudioTrack(int64_t id) noexcept = 0;

    // Platform playback devices. The first entry should be a synthetic default
    // device with an empty name. The callback receives stack-local POD snapshots.
    virtual void EnumerateAudioOutputDevices(
        void (*visit)(const AudioOutputDevice* device, void* ud), void* ud
    ) const noexcept = 0;

    // Apply user audio output, track-selection and behavior preferences.
    virtual void SetAudioPreferences(const AudioPreferences& prefs) noexcept = 0;

    // The currently-applied audio preferences (the last value passed to
    // SetAudioPreferences, or defaults if never set). Lets a UI read and round-trip
    // the live runtime prefs (e.g. sync-offset / output-device tweaks) without
    // holding its own copy.
    [[nodiscard]] virtual AudioPreferences GetAudioPreferences() const noexcept = 0;
};

// Subtitles: track selection, visibility, delay and appearance/behavior styling.
class ISubtitleControl
{
public:
    static constexpr const char* InterfaceId = "framelift.ISubtitleControl";
    virtual ~ISubtitleControl() = default;

    // Enumerate tracks: visit(track, ud) is called once per track.
    virtual void EnumerateSubtitleTracks(
        void (*visit)(const SubtitleTrack* track, void* ud), void* ud
    ) const noexcept = 0;
    virtual void SelectSubtitleTrack(int64_t id) noexcept = 0;
    virtual void ToggleSubtitles() noexcept = 0;
    [[nodiscard]] virtual bool IsSubtitlesEnabled() const noexcept = 0;
    virtual void CycleSubtitleTrack() noexcept = 0;
    virtual void AdjustSubtitleDelay(double delta) noexcept = 0;
    virtual void SetSubtitleDelay(double seconds) noexcept = 0;

    // Apply user subtitle appearance + selection preferences. Styling takes effect
    // on the next rendered frame; the behavior fields apply on the next LoadFile().
    virtual void SetSubtitleStyle(const SubtitleStyle& style) noexcept = 0;
};
