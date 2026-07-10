#pragma once

#include <framelift/platform/IMediaPlayer.h> // SubtitleStyle

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

// libass-backed subtitle pipeline for the FFmpeg backend (issue #8, Phase 5).
//
// This is an "FFmpeg* sibling" — together with FFmpegPlayer it is one of the only
// translation units permitted to include <ass/ass.h> / <libav*/...>. It owns the
// libass library + renderer + a single active track, guarded by a mutex so the
// subtitle decode thread (ProcessPacket / BeginTrack) and the render thread
// (RenderOverlay) never touch libass concurrently.
//
// Decoding is uniform across formats: libavcodec decodes ASS/SSA and SRT alike into
// AVSubtitle rects carrying ASS markup (it synthesises an ASS header for text subs),
// which is fed to ass_process_chunk. Rendering composites the resulting ASS_Image
// list into a straight-alpha RGBA8 overlay sized to the on-screen video rectangle.

// Opaque libass / libav types (defined only in the .cpp).
struct ass_library;
struct ass_renderer;
struct ass_track;
struct AVCodecContext;
struct AVPacket;
struct AVFormatContext;

class FFmpegSubtitles
{
public:
    // Outcome of a render: whether the caller should upload + draw the overlay, and
    // whether the pixel buffer was rewritten (so an unchanged frame skips re-upload).
    enum class RenderResult : std::uint8_t
    {
        None,      // nothing to draw this frame — do not composite the overlay
        Unchanged, // same as the previous frame — keep drawing the existing texture
        Updated,   // outRgba was rewritten — re-upload before drawing
    };

    FFmpegSubtitles();
    ~FFmpegSubtitles();

    FFmpegSubtitles(const FFmpegSubtitles&) = delete;
    FFmpegSubtitles& operator=(const FFmpegSubtitles&) = delete;

    // True when libass initialised (library + renderer). When false, all operations
    // are no-ops and RenderOverlay returns None.
    [[nodiscard]] bool Ok() const noexcept;

    // Start a fresh track seeded with a decoder's ASS codec header (header may be
    // null / size 0). Replaces any current track.
    void BeginTrack(const unsigned char* header, int headerSize);

    // Decode one subtitle packet and append its events to the current track.
    // tbNum/tbDen are the subtitle stream's time_base.
    void ProcessPacket(AVCodecContext* dec, AVPacket* pkt, int tbNum, int tbDen, double timelineStart);

    // Open an external subtitle file and pre-load ALL of its events into a fresh
    // track (events carry absolute timestamps, so no live demux/sync is needed).
    // Returns false if the file could not be opened/decoded.
    bool LoadExternalFile(const char* path);

    // Embedded-stream preload, split so the whole-container cue read
    // (the expensive part — it demuxes every packet of the file) can run off the
    // open path. BeginDeferredPreload does only the outcome-determining work
    // synchronously — opens the separate demuxer, validates the stream, opens the
    // decoder, and replaces the track seeded with the codec header — so the caller
    // keeps its live-decode fallback on failure. On success the caller must invoke
    // RunDeferredPreload (on any thread) exactly once to read/decode the cues; it
    // then feeds them to the track in bounded locked batches. The feed stops if the
    // preload is cancelled or the track was replaced meanwhile (generation check).
    bool BeginDeferredPreload(const char* path, int streamIndex, double timelineStart);
    void RunDeferredPreload();

    // Make a running RunDeferredPreload return early (its packet loop polls this).
    // The caller still joins its thread; the partial cues are discarded with it.
    void AbortPreload();

    // Drop all buffered events (used on seek for embedded/live tracks).
    void FlushEvents();

    // Make the next RenderOverlay rewrite the caller's pixel buffer even if libass
    // reports the same image list as the previous render.
    void ForceNextUpdate();

    // Discard the current track entirely (subtitles disabled / switching away).
    void ClearTrack();

    // Apply user subtitle appearance to the renderer. When style.overrideEnabled is
    // false, any previously installed override is cleared and the file's own style is
    // used. Forces the next RenderOverlay to re-rasterize so the change is visible
    // without a seek. (Only the appearance fields are consumed here; track-selection
    // fields are handled by the player.)
    void ApplyStyle(const SubtitleStyle& style);

    // Render subtitles for timeMs (playback time in ms) into outRgba — a tightly
    // packed RGBA8 image of size vpW*vpH (top row first, straight alpha) matching
    // the on-screen video rectangle. storageW/H are the native video size, used as
    // the ASS layout reference. See RenderResult for the contract.
    RenderResult RenderOverlay(
        int vpW, int vpH, int storageW, int storageH, long long timeMs, std::vector<unsigned char>& outRgba
    );

private:
    // Decode every subtitle packet of an opened format context (selected stream) into
    // the current track. Used by LoadExternalFile. Caller holds mutex_.
    bool PreloadFromFormatLocked(AVFormatContext* fmt, int streamIndex);

    // Shared setup for the sync/deferred preloads: pick/validate the subtitle
    // stream, open its decoder, and replace track_ seeded with the decoder's ASS
    // header (bumps trackGen_). Caller holds mutex_; on success the caller owns
    // outDec and must free it when done reading.
    bool SetupPreloadTrackLocked(AVFormatContext* fmt, int streamIndex, AVCodecContext*& outDec, int& outIdx);

    // Re-install the current style override on the renderer. Caller holds mutex_.
    void ApplyStyleLocked();

    // Demuxer/decoder handed from BeginDeferredPreload to RunDeferredPreload, which
    // takes sole ownership (reads them without mutex_) and frees them.
    struct PendingPreload
    {
        AVFormatContext* fmt = nullptr;
        AVCodecContext* dec = nullptr;
        int streamIndex = -1;
        double timelineStart = 0.0;
        std::uint64_t gen = 0; // trackGen_ at Begin — feed only while still current
    };

    mutable std::mutex mutex_;
    ass_library* lib_ = nullptr;
    ass_renderer* renderer_ = nullptr;
    ass_track* track_ = nullptr;

    SubtitleStyle style_{};        // current user override (overrideEnabled gates it)
    bool forceNextUpdate_ = false; // make the next RenderOverlay re-rasterize once
    PendingPreload pending_;       // guarded by mutex_ between Begin and Run
    std::uint64_t trackGen_ = 0;   // bumped on every track replacement (guarded by mutex_)
    std::atomic<bool> abortPreload_{false};
};
