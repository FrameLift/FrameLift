#include "FFmpegPlayer.h"

#include "FFmpegPlayerInternal.h"

#include "FFmpegAudioOutput.h"
#include "FFmpegClock.h"
#include "FFmpegPacketQueue.h"

#include "CacheSettings.h"         // ToReadAheadCacheOptions (host/read-ahead)
#include "FFmpegSettingsMapping.h" // To{PlaybackOptions,VideoDecodeMode,...}
#include "Settings.h"              // host aggregate settings

#include <algorithm>
#include <mutex>
#include <string>

// ── Playback commands ─────────────────────────────────────────────────────────

void FFmpegPlayer::LoadFile(const char* path, double resumePos) noexcept
{
    {
        std::lock_guard lock(mutex_);
        pendingPath_ = path ? path : "";
        pendingResume_ = resumePos;
        hasPendingLoad_ = true;
        // New file starts at a known origin; don't let a stale unsettled seek from the
        // previous file anchor the first relative seek on this one.
        seekSettled_ = true;
        seekClockValid_ = true;
        seekTarget_ = 0.0;
    }
    // Wake the decode thread and unblock any workers waiting on a queue so the
    // current file is abandoned promptly.
    audioQ_->Stop();
    videoQ_->Stop();
    subQ_->Stop();
    Wake();
}

void FFmpegPlayer::Stop() noexcept
{
    {
        std::lock_guard lock(mutex_);
        if (idle_.load())
        {
            return; // nothing loaded — already idle
        }
        stopRequested_ = true;   // break the decode thread out of playback / EOF hold
        hasPendingSeek_ = false; // drop any queued seek — there's nothing to resume
        boundaryEofRequested_ = false;
        forwardBoundaryRepeatLatched_ = false;
    }
    // Abandon the current file promptly (mirror LoadFile): unblock the workers and the
    // decode thread so it tears the session down and parks. stopRequested_ lingers while
    // parked (harmless — nothing consults it there) and is cleared by the next load.
    audioQ_->Stop();
    videoQ_->Stop();
    subQ_->Stop();
    Wake();

    // Reflect idle immediately for the UI: clears the EOF-held frame's seekable state
    // and raises IdleActive so the overlay shows the idle screen and hides the controls.
    eofReached_ = false;
    EmitFlag(PlayerProperty::EofReached, false);
    SetIdle(true);
}

void FFmpegPlayer::SetPause(bool paused) noexcept
{
    paused_ = paused;
    audioOut_->SetPaused(paused);
    {
        std::lock_guard lock(mutex_);
        videoClock_.OnPauseEdge(paused, std::chrono::steady_clock::now());
    }
    Wake();
    EmitFlag(PlayerProperty::Pause, paused);
    UpdateCoreIdle();
}

void FFmpegPlayer::TogglePause() noexcept
{
    SetPause(!paused_.load());
}

void FFmpegPlayer::ToggleMute() noexcept
{
    muteEnabled_ = !muteEnabled_;
    audioOut_->SetMute(muteEnabled_);
    EmitFlag(PlayerProperty::Mute, muteEnabled_);
}

void FFmpegPlayer::AdjustVolume(int delta) noexcept
{
    volume_ = std::clamp(volume_ + delta, 0, 100);
    audioOut_->SetVolume(volume_);
    EmitDouble(PlayerProperty::Volume, static_cast<double>(volume_));
}

void FFmpegPlayer::Seek(double seconds) noexcept
{
    (void)SeekRelativeFromInput(seconds, false);
}

RelativeSeekResult FFmpegPlayer::SeekRelativeFromInput(double seconds, bool autoRepeat) noexcept
{
    if (idle_.load())
    {
        return RelativeSeekResult::IgnoredAtStart; // don't seek the next file opened
    }
    // Accumulate relative seeks against the last requested target rather than the
    // master clock whenever a seek is still settling: while a seek is in flight or its
    // post-seek clock hasn't been re-established (e.g. a held arrow key auto-repeating),
    // the master clock reads ~0, so basing off it would make a repeat re-target from the
    // start instead of stepping further from the previous press.
    bool useAnchor = false;
    double anchor = 0.0;
    double previousTarget = 0.0;
    {
        std::lock_guard lock(mutex_);
        if (!autoRepeat)
        {
            forwardBoundaryRepeatLatched_ = false;
        }
        else if (seconds > 0.0 && forwardBoundaryRepeatLatched_)
        {
            return RelativeSeekResult::CompletedAtEnd;
        }
        // Anchor on seekClockValid_, not seekSettled_: the master clock (audio when a
        // device is open) may still read 0 even after the video frame has painted.
        useAnchor = hasPendingSeek_ || !seekClockValid_;
        anchor = seekTarget_;
        previousTarget = seekTarget_;
    }
    const double base = useAnchor ? anchor : GetMasterClock();
    const RelativeSeekDecision decision =
        DecideRelativeSeek(base, seconds, duration_.load(), autoRepeat, previousTarget);
    if (decision.result == RelativeSeekResult::IgnoredAtStart)
    {
        return decision.result;
    }
    if (decision.result == RelativeSeekResult::CompletedAtEnd)
    {
        RequestBoundaryEof();
        return decision.result;
    }

    RequestSeek(decision.target, DecideSeekKind(seekMode_.load(), /*relative=*/true, seconds, hasVideo_));
    return RelativeSeekResult::Applied;
}

void FFmpegPlayer::SeekAbsolute(double seconds) noexcept
{
    if (idle_.load())
    {
        return;
    }
    RequestSeek(
        ClampSeekTarget(seconds, duration_.load()), DecideSeekKind(seekMode_.load(), /*relative=*/false, 0.0, hasVideo_)
    );
}

void FFmpegPlayer::RequestSeek(double target, SeekKind kind) noexcept
{
    // Give every request a generation, even when it only updates an already-pending
    // target. The demux thread still takes just the latest target/generation, but a
    // request must never share an output generation with work that may already be in
    // flight for an earlier target.
    bool kick = false;
    std::uint64_t generation = 0;
    {
        std::lock_guard lock(mutex_);
        seekGeneration_ = nextGeneration_++;
        seekTarget_ = target; // latest-wins: coalesces rapid seeks (seek-bar drags)
        seekKind_ = kind;
        hasPendingSeek_ = true;
        boundaryEofRequested_ = false; // a seek from EOF hold resumes this file
        generation = seekGeneration_;
        kick = seekSettled_; // pipeline idle / already painted ⇒ safe to restart
        activeGeneration_.store(generation, std::memory_order_release);
        seekPerfGeneration_ = generation;
        seekPerfStarted_ = std::chrono::steady_clock::now();
        if (kick)
        {
            seekSettled_ = false; // re-settled by the worker that presents the post-seek frame
        }
    }
    // The audio discontinuity starts at the command, not after worker teardown.
    // FFmpegAudioOutput serialises this reset against Feed(), so either an old
    // feed lands first and is cleared, or it lands later and is rejected.
    const auto audioCutStarted = std::chrono::steady_clock::now();
    audioOut_->InterruptForSeek(generation);
    ffplay_detail::LogSeekPerf("seek-audio-cut", generation, audioCutStarted);
    // Every request cuts off the currently applied generation. This also releases a
    // demuxer blocked on queue backpressure; the decode loop then takes the latest
    // coalesced target without waiting for an obsolete frame to present.
    audioQ_->Interrupt();
    videoQ_->Interrupt();
    subQ_->Interrupt();
    Wake();
}

void FFmpegPlayer::RequestBoundaryEof() noexcept
{
    std::uint64_t generation = 0;
    {
        std::lock_guard lock(mutex_);
        if (boundaryEofRequested_ || eofReached_.load() || idle_.load())
        {
            return;
        }
        boundaryEofRequested_ = true;
        forwardBoundaryRepeatLatched_ = true;
        hasPendingSeek_ = false;
        seekTarget_ = duration_.load();
        seekSettled_ = true;
        seekClockValid_ = false; // a later backward seek anchors on seekTarget_
        generation = nextGeneration_++;
        activeGeneration_.store(generation, std::memory_order_release);
        seekPerfGeneration_ = 0; // a superseded in-file seek must not report a frame
    }

    // Cut the old generation before publishing EOF. This is the same immediate
    // PCM/device discontinuity used by an ordinary seek, but there is no decoder
    // flush/refill: Playlist will replace the file or Stop() the final item.
    audioOut_->InterruptForSeek(generation);
    audioQ_->Interrupt();
    videoQ_->Interrupt();
    subQ_->Interrupt();
    EmitFlag(PlayerProperty::Seeking, false);
    Log::Debug("FFmpegPlayer: relative seek completed file at end (generation {})", generation);
    Wake();
}

void FFmpegPlayer::SetImageDisplayDuration(double seconds) noexcept
{
    imageDisplayDuration_ = seconds; // <= 0 ⇒ hold a still image indefinitely
}

void FFmpegPlayer::SetAudioNormalize(bool enabled, const AudioNormalizeParams& params) noexcept
{
    bool changed = false;
    {
        std::lock_guard lock(mutex_);
        // ApplySettings() calls this on every settings commit; detect a no-op so unrelated
        // toggles don't force the seek-to-current below (which flushes the queues and,
        // while paused, audibly restarted the audio sink).
        const AudioNormalizeParams& cur = normalizeParams_;
        const bool paramsEqual = cur.algorithm == params.algorithm && cur.limiterLevelIn == params.limiterLevelIn &&
                                 cur.limiterLevelOut == params.limiterLevelOut &&
                                 cur.limiterLimit == params.limiterLimit && cur.limiterAttack == params.limiterAttack &&
                                 cur.limiterRelease == params.limiterRelease && cur.frameLen == params.frameLen &&
                                 cur.gaussSize == params.gaussSize && cur.peak == params.peak &&
                                 cur.maxGain == params.maxGain && cur.volume == params.volume;
        changed = enabled != normalizeEnabled_.load() || (enabled && !paramsEqual);
        normalizeParams_ = params;
    }
    normalizeEnabled_ = enabled;
    EmitFlag(PlayerProperty::Normalize, enabled);
    if (!changed)
    {
        return; // config unchanged — nothing to rebuild, don't disturb playback
    }
    normalizeGen_.fetch_add(1); // the next generation rebuilds the worker-local graph

    if (idle_.load())
    {
        return; // no file playing — applied when the next file's audio worker starts
    }
    // Force a seek-to-current (like SelectAudioTrack) so the queued audio is flushed and
    // re-decoded from here with the new setting — otherwise the change isn't heard until
    // the device buffer drains (potentially several seconds).
    const double target = ClampSeekTarget(GetMasterClock(), duration_.load());
    RequestSeek(target, SeekKind::Exact);
}

void FFmpegPlayer::SetPlaybackOptions(const PlaybackOptions& opts) noexcept
{
    // opts.hrSeek is superseded by SetSeekMode (playback.seekMode) and no longer read.
    hwdec_ = opts.hwdec; // try hardware decode on the next load
    subAutoLoad_ = opts.subAutoLoad;
    audioFileAutoLoad_ = opts.audioFileAutoLoad;
}

void FFmpegPlayer::SetVideoDecodeMode(VideoDecodeMode mode) noexcept
{
    videoDecodeMode_ = mode;
}

void FFmpegPlayer::SetSeekMode(SeekPrecisionMode mode) noexcept
{
    seekMode_ = mode; // decided per request; a pending seek keeps its already-decided kind
}

void FFmpegPlayer::SetReadAheadCache(const ReadAheadCacheOptions& opts) noexcept
{
    // Takes effect immediately; the new byte budget governs the next reservation.
    cache_.Configure(opts.enabled, opts.maxBytes);
}

void FFmpegPlayer::SetSubtitleStyle(const SubtitleStyle& style) noexcept
{
    {
        std::lock_guard lock(tracksMutex_);
        subtitleStyle_ = style; // behavior fields read by BuildTrackList on the decode thread
    }
    // Styling is renderer-level libass state and persists across tracks; applying it
    // here makes the change visible on the next rendered frame without a seek.
    if (subtitles_)
    {
        subtitles_->ApplyStyle(style);
    }
    RequestRender();
}

void FFmpegPlayer::ApplySettings(const Settings& s)
{
    SetPlaybackOptions(ToPlaybackOptions(s.Get<PlaybackSettings>()));
    SetVideoDecodeMode(ToVideoDecodeMode(s.Get<PlaybackSettings>()));
    SetSeekMode(ToSeekPrecisionMode(s.Get<PlaybackSettings>()));
    // Module-internal knob (not part of the SDK PlaybackOptions POD): applies to
    // the next avformat_open_input.
    fastProbe_ = s.Get<PlaybackSettings>().fastProbe;
    SetReadAheadCache(ToReadAheadCacheOptions(s.Get<CacheSettings>()));
    SetSubtitleStyle(ToSubtitleStyle(s.Get<SubtitleSettings>()));
    SetAudioPreferences(ToAudioPreferences(s.Get<AudioSettings>()));
    const AudioSettings& a = s.Get<AudioSettings>();
    SetAudioNormalize(a.normalizeEnabled, a.normalizeEnabled ? ToAudioNormalizeParams(a) : AudioNormalizeParams{});
}

void FFmpegPlayer::PulseDucking() noexcept
{
    audioOut_->PulseDuck();
}

void FFmpegPlayer::ToggleSubtitles() noexcept
{
    subtitlesEnabled_ = !subtitlesEnabled_;
    RequestRender(); // show/hide the overlay promptly, even while paused
}

void FFmpegPlayer::AdjustSubtitleDelay(double delta) noexcept
{
    subtitleDelay_ = subtitleDelay_.load() + delta;
    RequestRender();
}

void FFmpegPlayer::SetSubtitleDelay(double seconds) noexcept
{
    subtitleDelay_ = seconds;
    RequestRender();
}
