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
    audioQ_->Abort();
    videoQ_->Abort();
    subQ_->Abort();
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
    }
    // Abandon the current file promptly (mirror LoadFile): unblock the workers and the
    // decode thread so it tears the session down and parks. stopRequested_ lingers while
    // parked (harmless — nothing consults it there) and is cleared by the next load.
    audioQ_->Abort();
    videoQ_->Abort();
    subQ_->Abort();
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
        const auto now = std::chrono::steady_clock::now();
        if (paused)
        {
            pauseWall_ = now;
        }
        else if (videoClockSet_)
        {
            videoClockWall_ += now - pauseWall_; // shift baseline past the pause gap
        }
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
    if (idle_.load())
    {
        return; // nothing loaded — ignore (avoids seeking the next file opened)
    }
    // Accumulate relative seeks against the last requested target rather than the
    // master clock whenever a seek is still settling: while a seek is in flight or its
    // post-seek clock hasn't been re-established (e.g. a held arrow key auto-repeating),
    // the master clock reads ~0, so basing off it would make a repeat re-target from the
    // start instead of stepping further from the previous press.
    bool useAnchor = false;
    double anchor = 0.0;
    {
        std::lock_guard lock(mutex_);
        // Anchor on seekClockValid_, not seekSettled_: the master clock (audio when a
        // device is open) may still read 0 even after the video frame has painted.
        useAnchor = hasPendingSeek_ || !seekClockValid_;
        anchor = seekTarget_;
    }
    const double base = useAnchor ? anchor : GetMasterClock();
    RequestSeek(ClampSeekTarget(base + seconds, duration_.load()));
}

void FFmpegPlayer::SeekAbsolute(double seconds) noexcept
{
    if (idle_.load())
    {
        return;
    }
    RequestSeek(ClampSeekTarget(seconds, duration_.load()));
}

void FFmpegPlayer::RequestSeek(double target) noexcept
{
    // Only disturb the running pipeline when the previous seek has already painted a
    // frame (or none is in flight). While a seek is mid-decode, a new request — e.g. a
    // held arrow key auto-repeating ~30x/s — just updates the target (latest-wins) and
    // lets the in-flight seek present first; the decode loop then re-seeks to the newer
    // target. Aborting on every repeat instead would tear the decoder down before it ever
    // reaches the target, freezing the picture until the key is released.
    bool kick = false;
    {
        std::lock_guard lock(mutex_);
        seekTarget_ = target; // latest-wins: coalesces rapid seeks (seek-bar drags)
        hasPendingSeek_ = true;
        kick = seekSettled_; // pipeline idle / already painted ⇒ safe to restart
        if (kick)
        {
            seekSettled_ = false; // re-settled by the worker that presents the post-seek frame
        }
    }
    if (kick)
    {
        // Perf timing: measure each applied seek (not each coalesced repeat) to its frame.
        FRAMELIFT_PERF_START("seek");
        // Unblock the demux read loop / keep-open wait and any worker mid-present.
        audioQ_->Abort();
        videoQ_->Abort();
        subQ_->Abort();
        Wake();
    }
}

void FFmpegPlayer::SetImageDisplayDuration(double seconds) noexcept
{
    imageDisplayDuration_ = seconds; // <= 0 ⇒ hold a still image indefinitely
}

void FFmpegPlayer::SetAudioNormalize(bool enabled, const AudioNormalizeParams& params) noexcept
{
    {
        std::lock_guard lock(mutex_);
        normalizeParams_ = params;
    }
    normalizeEnabled_ = enabled;
    normalizeGen_.fetch_add(1); // the respawned worker rebuilds its graph from the flag
    EmitFlag(PlayerProperty::Normalize, enabled);

    if (idle_.load())
    {
        return; // no file playing — applied when the next file's audio worker starts
    }
    // Force a seek-to-current (like SelectAudioTrack) so the queued audio is flushed and
    // re-decoded from here with the new setting — otherwise the change isn't heard until
    // the device buffer drains (potentially several seconds).
    const double target = ClampSeekTarget(GetMasterClock(), duration_.load());
    {
        std::lock_guard lock(mutex_);
        seekTarget_ = target;
        hasPendingSeek_ = true;
    }
    audioQ_->Abort();
    videoQ_->Abort();
    subQ_->Abort();
    Wake();
}

void FFmpegPlayer::SetPlaybackOptions(const PlaybackOptions& opts) noexcept
{
    hrSeek_ = opts.hrSeek; // exact vs keyframe seeking
    hwdec_ = opts.hwdec;   // try hardware decode on the next load
    subAutoLoad_ = opts.subAutoLoad;
    audioFileAutoLoad_ = opts.audioFileAutoLoad;
}

void FFmpegPlayer::SetVideoDecodeMode(VideoDecodeMode mode) noexcept
{
    videoDecodeMode_ = mode;
}

void FFmpegPlayer::SetReadAheadCache(const ReadAheadCacheOptions& opts) noexcept
{
    // Takes effect immediately; the new byte budget governs the next WaitForSpace.
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
