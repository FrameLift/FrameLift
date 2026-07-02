#include "FFmpegPlayer.h"

#include "FFmpegPlayerInternal.h"

#include "FFmpegAudioOutput.h"
#include "FFmpegClock.h"
#include "FFmpegPacketQueue.h"
#include "FFmpegTrackLabel.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

using namespace ffplay_detail;

// ── Track model ─────────────────────────────────────────────────────────────

void FFmpegPlayer::BuildTrackList(AVFormatContext* mainFmt, int defaultAudioStream)
{
    // The only libav part: snapshot the embedded audio/subtitle streams for the
    // pure selection logic in FFmpegTrackSelect.h.
    std::vector<EmbeddedStreamInfo> streams;
    for (unsigned i = 0; i < mainFmt->nb_streams; ++i)
    {
        const AVStream* st = mainFmt->streams[i];
        const AVMediaType type = st->codecpar->codec_type;
        if (type != AVMEDIA_TYPE_AUDIO && type != AVMEDIA_TYPE_SUBTITLE)
        {
            continue;
        }
        const AVDictionaryEntry* titleTag = av_dict_get(st->metadata, "title", nullptr, 0);
        const AVDictionaryEntry* langTag = av_dict_get(st->metadata, "language", nullptr, 0);
        EmbeddedStreamInfo info;
        info.index = static_cast<int>(i);
        info.isAudio = type == AVMEDIA_TYPE_AUDIO;
        info.title = titleTag && titleTag->value ? titleTag->value : "";
        info.language = langTag && langTag->value ? langTag->value : "";
        info.isDefault = (st->disposition & AV_DISPOSITION_DEFAULT) != 0;
        info.isForced = (st->disposition & AV_DISPOSITION_FORCED) != 0;
        streams.push_back(std::move(info));
    }

    std::lock_guard lock(tracksMutex_);
    // User track-selection preferences (guarded by tracksMutex_, held here).
    TrackSelection sel = BuildTracks(
        streams, externalSources_, defaultAudioStream, audioPrefs_.preferredLang, subtitleStyle_.preferredLang,
        subtitleStyle_.preferForced
    );
    tracks_ = std::move(sel.tracks);
    selectedAudioId_ = sel.selectedAudioId;
    selectedSubId_ = sel.selectedSubId;
    nextTrackId_ = sel.nextTrackId;
}

void FFmpegPlayer::RefreshSelectedFlags()
{
    std::lock_guard lock(tracksMutex_);
    for (TrackEntry& t : tracks_)
    {
        t.selected = (t.kind == TrackKind::Audio && t.id == selectedAudioId_) ||
                     (t.kind == TrackKind::Subtitle && t.id == selectedSubId_);
    }
}

bool FFmpegPlayer::FindTrack(int64_t id, TrackEntry& out) const
{
    std::lock_guard lock(tracksMutex_);
    for (const TrackEntry& t : tracks_)
    {
        if (t.id == id)
        {
            out = t;
            return true;
        }
    }
    return false;
}

bool FFmpegPlayer::OpenAudioBinding(int64_t id, AVFormatContext* mainFmt, AudioBinding& aud)
{
    // Tear down any existing binding (close external context + audio device).
    if (aud.dec)
    {
        avcodec_free_context(&aud.dec);
    }
    if (aud.external && aud.fmt)
    {
        avformat_close_input(&aud.fmt);
    }
    aud = AudioBinding{};
    // Note: the audio device is NOT closed here. FFmpegAudioOutput::Open() reuses a
    // still-running sink when the new track's output format matches, so on the common
    // file→file boundary the QAudioSink + its thread stay up. Every path below that ends
    // without a live binding closes the device explicitly instead.

    TrackEntry e;
    if (id < 0 || !FindTrack(id, e) || e.kind != TrackKind::Audio)
    {
        audioOut_->Close();
        return false;
    }

    AVFormatContext* srcFmt = nullptr;
    int streamIdx = -1;
    if (!e.external)
    {
        srcFmt = mainFmt;
        streamIdx = e.streamIndex;
    }
    else
    {
        const std::string& srcPath = externalSources_[e.container - 1].path;
        InstallFFmpegLogCallback(); // re-assert; Qt may have clobbered the global callback.
        if (avformat_open_input(&srcFmt, srcPath.c_str(), nullptr, nullptr) < 0)
        {
            Log::Warn("FFmpegPlayer: failed to open external audio {}", srcPath);
            audioOut_->Close();
            return false;
        }
        if (avformat_find_stream_info(srcFmt, nullptr) < 0)
        {
            avformat_close_input(&srcFmt);
            audioOut_->Close();
            return false;
        }
        streamIdx = av_find_best_stream(srcFmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (streamIdx < 0)
        {
            avformat_close_input(&srcFmt);
            audioOut_->Close();
            return false;
        }
    }

    AVStream* st = srcFmt->streams[streamIdx];
    const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
    AVCodecContext* dec = codec ? avcodec_alloc_context3(codec) : nullptr;
    if (!dec)
    {
        if (e.external)
        {
            avformat_close_input(&srcFmt);
        }
        audioOut_->Close();
        return false;
    }
    avcodec_parameters_to_context(dec, st->codecpar);
    dec->pkt_timebase = st->time_base;
    if (avcodec_open2(dec, codec, nullptr) != 0 || !audioOut_->Open(dec->sample_rate, dec->ch_layout, dec->sample_fmt))
    {
        Log::Warn("FFmpegPlayer: audio decoder/output unavailable for track {}", id);
        avcodec_free_context(&dec);
        audioOut_->Close();
        if (e.external)
        {
            avformat_close_input(&srcFmt);
        }
        return false;
    }
    audioOut_->SetVolume(volume_);
    audioOut_->SetMute(muteEnabled_);

    aud.fmt = srcFmt;
    aud.dec = dec;
    aud.stream = st;
    aud.streamIndex = streamIdx;
    aud.external = e.external;
    aud.startOffset = e.external && st->start_time != AV_NOPTS_VALUE
                          ? static_cast<double>(st->start_time) * av_q2d(st->time_base)
                          : 0.0;

    {
        std::lock_guard lock(tracksMutex_);
        selectedAudioId_ = id;
    }
    RefreshSelectedFlags();
    return true;
}

void FFmpegPlayer::JoinSubtitlePreload()
{
    if (subtitlePreloadThread_.joinable())
    {
        subtitles_->AbortPreload();
        subtitlePreloadThread_.join();
    }
}

void FFmpegPlayer::OpenSubtitleBinding(
    int64_t id, const std::string& mediaPath, AVFormatContext* mainFmt, int& subIdx, AVCodecContext*& sDec,
    AVStream*& sStream
)
{
    JoinSubtitlePreload(); // the track is replaced below — no reader may be in flight
    if (sDec)
    {
        avcodec_free_context(&sDec);
    }
    sDec = nullptr;
    sStream = nullptr;
    subIdx = -1;
    subtitles_->ClearTrack();

    {
        std::lock_guard lock(tracksMutex_);
        selectedSubId_ = id;
    }
    RefreshSelectedFlags();

    TrackEntry e;
    if (id < 0 || !subtitles_->Ok() || !FindTrack(id, e) || e.kind != TrackKind::Subtitle)
    {
        return; // subtitles off / unavailable
    }

    if (e.external)
    {
        // External sidecar: pre-load all events; no embedded routing or worker.
        subtitles_->LoadExternalFile(externalSources_[e.container - 1].path.c_str());
        return;
    }

    // Embedded subtitles need the same absolute-event model as sidecars so a seek
    // can render the active cue immediately, even when that cue began before the
    // seek target. Use a separate input so the playback demuxer stays untouched.
    // The cue read demuxes the whole container, so only the outcome-determining
    // open runs here; the read continues on its own thread while the first frame
    // is already on screen (cues appear once loaded, via the RequestRender).
    if (subtitles_->BeginDeferredPreload(mediaPath.c_str(), e.streamIndex))
    {
        subtitlePreloadThread_ = std::thread(
            [this]
            {
                subtitles_->RunDeferredPreload();
                RequestRender();
            }
        );
        return;
    }

    // Fallback: live-decode embedded packets if the separate preload cannot open.
    AVStream* st = mainFmt->streams[e.streamIndex];
    const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
    AVCodecContext* dec = codec ? avcodec_alloc_context3(codec) : nullptr;
    if (!dec)
    {
        return;
    }
    avcodec_parameters_to_context(dec, st->codecpar);
    dec->pkt_timebase = st->time_base;
    if (avcodec_open2(dec, codec, nullptr) != 0)
    {
        avcodec_free_context(&dec);
        return;
    }
    subtitles_->BeginTrack(dec->subtitle_header, dec->subtitle_header_size);
    sDec = dec;
    sStream = st;
    subIdx = e.streamIndex;
}

// ── Subtitle / audio tracks ───────────────────────────────────────────────────

void FFmpegPlayer::SetAudioPreferences(const AudioPreferences& prefs) noexcept
{
    AudioPreferences old;
    {
        std::lock_guard lock(tracksMutex_);
        old = audioPrefs_;
        audioPrefs_ = prefs; // preferred language is read by BuildTrackList on the decode thread
    }

    audioSyncOffsetMs_ = prefs.syncOffsetMs;
    const bool outputChanged =
        std::strcmp(old.outputDevice, prefs.outputDevice) != 0 || old.channelMode != prefs.channelMode;
    volume_ = std::clamp(prefs.defaultVolume, 0, 100);
    audioOut_->SetPreferences(prefs);
    audioOut_->SetVolume(volume_);
    EmitDouble(PlayerProperty::Volume, static_cast<double>(volume_));

    if (outputChanged && audioOut_->HasDevice() && !idle_.load())
    {
        const double target = ClampSeekTarget(GetMasterClock(), duration_.load());
        RequestSeek(target);
        audioQ_->Abort();
        videoQ_->Abort();
        subQ_->Abort();
    }
}

AudioPreferences FFmpegPlayer::GetAudioPreferences() const noexcept
{
    std::lock_guard lock(tracksMutex_);
    return audioPrefs_;
}

void FFmpegPlayer::CycleSubtitleTrack() noexcept
{
    // Advance off → first → … → last → off, then apply via the switch path.
    int64_t next = -1;
    {
        std::lock_guard lock(tracksMutex_);
        std::vector<int64_t> subs;
        for (const TrackEntry& t : tracks_)
        {
            if (t.kind == TrackKind::Subtitle)
            {
                subs.push_back(t.id);
            }
        }
        if (!subs.empty())
        {
            const auto it = std::find(subs.begin(), subs.end(), selectedSubId_);
            if (it == subs.end())
            {
                next = subs.front();
            }
            else
            {
                const auto nextIt = it + 1;
                next = nextIt == subs.end() ? -1 : *nextIt;
            }
        }
    }
    SelectSubtitleTrack(next);
}

void FFmpegPlayer::EnumerateSubtitleTracks(void (*visit)(const SubtitleTrack*, void*), void* ud) const noexcept
{
    if (!visit)
    {
        return;
    }
    std::lock_guard lock(tracksMutex_);
    for (const TrackEntry& t : tracks_)
    {
        if (t.kind != TrackKind::Subtitle)
        {
            continue;
        }
        SubtitleTrack s{};
        s.id = t.id;
        s.selected = t.selected;
        std::snprintf(s.label, sizeof(s.label), "%s", t.label.c_str());
        visit(&s, ud);
    }
}

void FFmpegPlayer::SelectSubtitleTrack(int64_t id) noexcept
{
    if (idle_.load())
    {
        return;
    }
    // Force a seek-to-current so the decode thread rebuilds the subtitle binding at
    // the seek boundary (re-feeding events for an embedded track from this point).
    const double target = ClampSeekTarget(GetMasterClock(), duration_.load());
    {
        std::lock_guard lock(mutex_);
        pendingSubId_ = id;
        hasPendingSubSwitch_ = true;
        seekTarget_ = target;
        hasPendingSeek_ = true;
    }
    audioQ_->Abort();
    videoQ_->Abort();
    subQ_->Abort();
    Wake();
}

void FFmpegPlayer::EnumerateAudioTracks(void (*visit)(const AudioTrack*, void*), void* ud) const noexcept
{
    if (!visit)
    {
        return;
    }
    std::lock_guard lock(tracksMutex_);
    for (const TrackEntry& t : tracks_)
    {
        if (t.kind != TrackKind::Audio)
        {
            continue;
        }
        AudioTrack a{};
        a.id = t.id;
        a.selected = t.selected;
        std::snprintf(a.label, sizeof(a.label), "%s", t.label.c_str());
        visit(&a, ud);
    }
}

void FFmpegPlayer::SelectAudioTrack(int64_t id) noexcept
{
    if (idle_.load())
    {
        return;
    }
    const double target = ClampSeekTarget(GetMasterClock(), duration_.load());
    {
        std::lock_guard lock(mutex_);
        pendingAudioId_ = id;
        hasPendingAudioSwitch_ = true;
        seekTarget_ = target;
        hasPendingSeek_ = true;
    }
    audioQ_->Abort();
    videoQ_->Abort();
    subQ_->Abort();
    Wake();
}
