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

void FFmpegPlayer::ScanExternalSources(const std::string& mediaPath)
{
    externalSources_.clear();
    if (!subAutoLoad_ && !audioFileAutoLoad_)
    {
        return;
    }

    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path media(mediaPath);
    const fs::path dir = media.parent_path();
    const std::string stem = media.stem().string();
    if (dir.empty() || stem.empty())
    {
        return;
    }

    const auto lower = [](std::string s)
    {
        for (char& c : s)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return s;
    };
    const std::string stemL = lower(stem);
    const std::string mediaName = media.filename().string();

    static constexpr std::array<const char*, 4> kSubExt = {".srt", ".ass", ".ssa", ".sub"};
    static constexpr std::array<const char*, 6> kAudExt = {".mka", ".m4a", ".aac", ".ac3", ".dts", ".flac"};

    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
    {
        if (!it->is_regular_file(ec))
        {
            continue;
        }
        const fs::path p = it->path();
        if (p.filename().string() == mediaName)
        {
            continue; // skip the media file itself
        }
        const std::string nameL = lower(p.filename().string());
        if (nameL.find(stemL) == std::string::npos)
        {
            continue; // fuzzy match: sidecar name must contain the media stem
        }
        const std::string ext = lower(p.extension().string());
        const auto matches = [&ext](const auto& list)
        {
            return std::find_if(
                       list.begin(), list.end(),
                       [&](const char* e)
                       {
                           return ext == e;
                       }
                   ) != list.end();
        };
        if (subAutoLoad_ && matches(kSubExt))
        {
            externalSources_.push_back({p.string(), false});
        }
        else if (audioFileAutoLoad_ && matches(kAudExt))
        {
            externalSources_.push_back({p.string(), true});
        }
    }
}

void FFmpegPlayer::BuildTrackList(AVFormatContext* mainFmt, int defaultAudioStream)
{
    std::lock_guard lock(tracksMutex_);
    tracks_.clear();
    nextTrackId_ = 1;

    int64_t defaultAudio = -1;
    int64_t defaultSub = -1;         // an embedded sub flagged DEFAULT
    int64_t defaultSubFallback = -1; // first subtitle track of any kind
    int audioOrd = 0;
    int subOrd = 0;

    // User track-selection preferences (guarded by tracksMutex_, held here).
    const std::string prefLang = subtitleStyle_.preferredLang;
    const std::string audioPrefLang = audioPrefs_.preferredLang;
    const bool preferForced = subtitleStyle_.preferForced;
    int64_t langAudio = -1;     // first audio stream matching the preferred language
    int64_t langSub = -1;       // first sub matching the preferred language
    int64_t forcedSub = -1;     // first forced sub
    int64_t forcedLangSub = -1; // first forced sub matching the preferred language

    // Case-insensitive language match tolerant of 2- vs 3-letter codes ("en"~"eng").
    const auto langMatches = [](const std::string& wanted, const char* tag) -> bool
    {
        if (wanted.empty() || !tag)
        {
            return false;
        }
        const size_t n = std::min(wanted.size(), std::strlen(tag));
        for (size_t i = 0; i < n; ++i)
        {
            if (std::tolower(static_cast<unsigned char>(tag[i])) != std::tolower(static_cast<unsigned char>(wanted[i])))
            {
                return false;
            }
        }
        return n > 0;
    };

    // Embedded audio + subtitle streams, in container order.
    for (unsigned i = 0; i < mainFmt->nb_streams; ++i)
    {
        AVStream* st = mainFmt->streams[i];
        const AVMediaType type = st->codecpar->codec_type;
        if (type != AVMEDIA_TYPE_AUDIO && type != AVMEDIA_TYPE_SUBTITLE)
        {
            continue;
        }
        const AVDictionaryEntry* titleTag = av_dict_get(st->metadata, "title", nullptr, 0);
        const AVDictionaryEntry* langTag = av_dict_get(st->metadata, "language", nullptr, 0);

        TrackEntry e;
        e.id = nextTrackId_++;
        e.kind = type == AVMEDIA_TYPE_AUDIO ? TrackKind::Audio : TrackKind::Subtitle;
        e.container = 0;
        e.streamIndex = static_cast<int>(i);
        e.external = false;
        const int ord = type == AVMEDIA_TYPE_AUDIO ? ++audioOrd : ++subOrd;
        char label[256];
        MakeTrackLabel(label, titleTag ? titleTag->value : nullptr, langTag ? langTag->value : nullptr, ord, nullptr);
        e.label = label;
        e.language = langTag ? langTag->value : "";

        if (type == AVMEDIA_TYPE_AUDIO && static_cast<int>(i) == defaultAudioStream)
        {
            defaultAudio = e.id;
        }
        if (type == AVMEDIA_TYPE_AUDIO && langAudio < 0 &&
            langMatches(audioPrefLang, langTag ? langTag->value : nullptr))
        {
            langAudio = e.id;
        }
        if (type == AVMEDIA_TYPE_SUBTITLE)
        {
            if (defaultSubFallback < 0)
            {
                defaultSubFallback = e.id;
            }
            if ((st->disposition & AV_DISPOSITION_DEFAULT) != 0 && defaultSub < 0)
            {
                defaultSub = e.id;
            }
            const bool forced = (st->disposition & AV_DISPOSITION_FORCED) != 0;
            const bool langOk = langMatches(prefLang, langTag ? langTag->value : nullptr);
            if (langOk && langSub < 0)
            {
                langSub = e.id;
            }
            if (forced && forcedSub < 0)
            {
                forcedSub = e.id;
            }
            if (forced && langOk && forcedLangSub < 0)
            {
                forcedLangSub = e.id;
            }
        }
        tracks_.push_back(std::move(e));
    }

    if (defaultAudio < 0)
    {
        for (const TrackEntry& t : tracks_)
        {
            if (t.kind == TrackKind::Audio)
            {
                defaultAudio = t.id;
                break;
            }
        }
    }

    // External sidecar files (their container index is externalSources_ index + 1).
    for (std::size_t k = 0; k < externalSources_.size(); ++k)
    {
        const ExternalSource& src = externalSources_[k];
        const std::string base = std::filesystem::path(src.path).filename().string();
        TrackEntry e;
        e.id = nextTrackId_++;
        e.kind = src.isAudio ? TrackKind::Audio : TrackKind::Subtitle;
        e.container = static_cast<int>(k) + 1;
        e.streamIndex = -1;
        e.external = true;
        char label[256];
        MakeTrackLabel(label, nullptr, nullptr, 0, base.c_str());
        e.label = label;
        if (!src.isAudio && defaultSubFallback < 0)
        {
            defaultSubFallback = e.id;
        }
        tracks_.push_back(std::move(e));
    }

    selectedAudioId_ = langAudio >= 0 ? langAudio : defaultAudio;

    // Selection precedence: forced (matching language first) when preferForced, then a
    // preferred-language match, then the file's DEFAULT-flagged sub, then any sub.
    int64_t chosenSub = -1;
    if (preferForced)
    {
        chosenSub = forcedLangSub >= 0 ? forcedLangSub : forcedSub;
    }
    if (chosenSub < 0)
    {
        chosenSub = langSub;
    }
    if (chosenSub < 0)
    {
        chosenSub = defaultSub >= 0 ? defaultSub : defaultSubFallback;
    }
    selectedSubId_ = chosenSub;
    for (TrackEntry& t : tracks_)
    {
        t.selected = (t.kind == TrackKind::Audio && t.id == selectedAudioId_) ||
                     (t.kind == TrackKind::Subtitle && t.id == selectedSubId_);
    }
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

void FFmpegPlayer::OpenSubtitleBinding(
    int64_t id, const std::string& mediaPath, AVFormatContext* mainFmt, int& subIdx, AVCodecContext*& sDec,
    AVStream*& sStream
)
{
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
    if (subtitles_->LoadEmbeddedStream(mediaPath.c_str(), e.streamIndex))
    {
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
