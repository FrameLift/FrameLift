#pragma once

#include "FFmpegSidecarScan.h"
#include "FFmpegTrackLabel.h"

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

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
    int container = 0;    // 0 = main container; >=1 == external-source index + 1
    int streamIndex = -1; // stream index within that container (embedded routing)
    bool external = false;
    bool selected = false;
    std::string label;
    std::string language;
};

// The libav-free snapshot of one embedded audio/subtitle stream that track
// selection needs (filled from AVStream by the caller — the only libav part).
struct EmbeddedStreamInfo
{
    int index = -1;       // stream index in the main container
    bool isAudio = false; // else subtitle
    std::string title;    // metadata "title" tag, empty when absent
    std::string language; // metadata "language" tag, empty when absent
    bool isDefault = false; // AV_DISPOSITION_DEFAULT
    bool isForced = false;  // AV_DISPOSITION_FORCED
};

struct TrackSelection
{
    std::vector<TrackEntry> tracks;
    int64_t selectedAudioId = -1;
    int64_t selectedSubId = -1; // -1 == subtitles off / none
    int64_t nextTrackId = 1;
};

// Case-insensitive language match tolerant of 2- vs 3-letter codes ("en"~"eng").
inline bool TrackLangMatches(const std::string& wanted, const std::string& tag)
{
    if (wanted.empty() || tag.empty())
    {
        return false;
    }
    const size_t n = std::min(wanted.size(), tag.size());
    for (size_t i = 0; i < n; ++i)
    {
        if (std::tolower(static_cast<unsigned char>(tag[i])) != std::tolower(static_cast<unsigned char>(wanted[i])))
        {
            return false;
        }
    }
    return n > 0;
}

// Build the selectable track list from the main container's audio/subtitle
// streams (in container order) + discovered sidecar files, and choose the
// default audio and subtitle selection.
//
// Audio precedence: preferred-language match, else the container's default
// stream, else the first audio track. Subtitle precedence: forced (matching
// language first) when preferForced, then a preferred-language match, then the
// DEFAULT-flagged sub, then any sub (external sidecars only as last fallback).
// Pure (no libav) → unit-tested in the native suite.
inline TrackSelection BuildTracks(const std::vector<EmbeddedStreamInfo>& streams,
                                  const std::vector<ExternalSource>& externals, int defaultAudioStream,
                                  const std::string& audioPrefLang, const std::string& subPrefLang, bool preferForced)
{
    TrackSelection out;

    int64_t defaultAudio = -1;
    int64_t defaultSub = -1;         // an embedded sub flagged DEFAULT
    int64_t defaultSubFallback = -1; // first subtitle track of any kind
    int audioOrd = 0;
    int subOrd = 0;

    int64_t langAudio = -1;     // first audio stream matching the preferred language
    int64_t langSub = -1;       // first sub matching the preferred language
    int64_t forcedSub = -1;     // first forced sub
    int64_t forcedLangSub = -1; // first forced sub matching the preferred language

    // Embedded audio + subtitle streams, in container order.
    for (const EmbeddedStreamInfo& st : streams)
    {
        TrackEntry e;
        e.id = out.nextTrackId++;
        e.kind = st.isAudio ? TrackKind::Audio : TrackKind::Subtitle;
        e.container = 0;
        e.streamIndex = st.index;
        e.external = false;
        const int ord = st.isAudio ? ++audioOrd : ++subOrd;
        char label[256];
        MakeTrackLabel(label, st.title.c_str(), st.language.c_str(), ord, nullptr);
        e.label = label;
        e.language = st.language;

        if (st.isAudio && st.index == defaultAudioStream)
        {
            defaultAudio = e.id;
        }
        if (st.isAudio && langAudio < 0 && TrackLangMatches(audioPrefLang, st.language))
        {
            langAudio = e.id;
        }
        if (!st.isAudio)
        {
            if (defaultSubFallback < 0)
            {
                defaultSubFallback = e.id;
            }
            if (st.isDefault && defaultSub < 0)
            {
                defaultSub = e.id;
            }
            const bool langOk = TrackLangMatches(subPrefLang, st.language);
            if (langOk && langSub < 0)
            {
                langSub = e.id;
            }
            if (st.isForced && forcedSub < 0)
            {
                forcedSub = e.id;
            }
            if (st.isForced && langOk && forcedLangSub < 0)
            {
                forcedLangSub = e.id;
            }
        }
        out.tracks.push_back(std::move(e));
    }

    if (defaultAudio < 0)
    {
        for (const TrackEntry& t : out.tracks)
        {
            if (t.kind == TrackKind::Audio)
            {
                defaultAudio = t.id;
                break;
            }
        }
    }

    // External sidecar files (their container index is the external index + 1).
    for (std::size_t k = 0; k < externals.size(); ++k)
    {
        const ExternalSource& src = externals[k];
        const std::string base = std::filesystem::path(src.path).filename().string();
        TrackEntry e;
        e.id = out.nextTrackId++;
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
        out.tracks.push_back(std::move(e));
    }

    out.selectedAudioId = langAudio >= 0 ? langAudio : defaultAudio;

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
    out.selectedSubId = chosenSub;
    for (TrackEntry& t : out.tracks)
    {
        t.selected = (t.kind == TrackKind::Audio && t.id == out.selectedAudioId) ||
                     (t.kind == TrackKind::Subtitle && t.id == out.selectedSubId);
    }
    return out;
}
