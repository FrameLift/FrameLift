// Unit tests for BuildTracks — the pure track-list construction + default
// audio/subtitle selection precedence extracted from FFmpegPlayer::BuildTrackList.

#include "FFmpegTrackSelect.h"

#include "QtTestRunner.h"

#include <QtTest/QtTest>

#include <string>
#include <vector>

namespace
{
EmbeddedStreamInfo Audio(int index, const std::string& lang = "", const std::string& title = "")
{
    EmbeddedStreamInfo s;
    s.index = index;
    s.isAudio = true;
    s.title = title;
    s.language = lang;
    return s;
}

EmbeddedStreamInfo Sub(int index, const std::string& lang = "", bool isDefault = false, bool isForced = false)
{
    EmbeddedStreamInfo s;
    s.index = index;
    s.isAudio = false;
    s.language = lang;
    s.isDefault = isDefault;
    s.isForced = isForced;
    return s;
}

const TrackEntry* ById(const TrackSelection& sel, int64_t id)
{
    for (const TrackEntry& t : sel.tracks)
    {
        if (t.id == id)
        {
            return &t;
        }
    }
    return nullptr;
}
} // namespace

class FFmpegTrackSelectTests final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void IdsAreStableAndOrdered()
    {
        const auto sel = BuildTracks({Audio(1), Sub(2), Audio(3)}, {}, -1, "", "", false);
        QCOMPARE(sel.tracks.size(), std::size_t{3});
        QCOMPARE(sel.tracks[0].id, int64_t{1});
        QCOMPARE(sel.tracks[1].id, int64_t{2});
        QCOMPARE(sel.tracks[2].id, int64_t{3});
        QCOMPARE(sel.nextTrackId, int64_t{4});
        QCOMPARE(sel.tracks[0].streamIndex, 1);
        QCOMPARE(sel.tracks[1].kind, TrackKind::Subtitle);
    }

    void DefaultAudioFallsBackToFirstAudioTrack()
    {
        // defaultAudioStream doesn't match any stream index → first audio wins.
        const auto sel = BuildTracks({Sub(0), Audio(1), Audio(2)}, {}, -1, "", "", false);
        QCOMPARE(sel.selectedAudioId, int64_t{2}); // Audio(1) got id 2
        QVERIFY(ById(sel, 2)->selected);
    }

    void ContainerDefaultAudioStreamIsHonored()
    {
        const auto sel = BuildTracks({Audio(0), Audio(1)}, {}, 1, "", "", false);
        QCOMPARE(sel.selectedAudioId, int64_t{2}); // the stream with index 1
    }

    void PreferredAudioLanguageBeatsContainerDefault()
    {
        const auto sel = BuildTracks({Audio(0, "eng"), Audio(1, "fin")}, {}, 0, "fi", "", false);
        QCOMPARE(sel.selectedAudioId, int64_t{2}); // "fi" matches "fin" (2- vs 3-letter)
    }

    void SubtitlePrecedenceForcedLangOverForced()
    {
        const auto streams = std::vector<EmbeddedStreamInfo>{
            Sub(0, "eng", false, true), // forced, wrong language
            Sub(1, "fin", false, true), // forced + preferred language → wins
            Sub(2, "fin", true, false), // DEFAULT-flagged
        };
        const auto sel = BuildTracks(streams, {}, -1, "", "fi", true);
        QCOMPARE(sel.selectedSubId, int64_t{2});
    }

    void SubtitlePrecedenceLangOverDefaultFlag()
    {
        const auto streams = std::vector<EmbeddedStreamInfo>{
            Sub(0, "eng", true, false), // DEFAULT-flagged
            Sub(1, "fin", false, false), // preferred language → wins without preferForced
        };
        const auto sel = BuildTracks(streams, {}, -1, "", "fi", false);
        QCOMPARE(sel.selectedSubId, int64_t{2});
    }

    void SubtitlePrecedenceDefaultFlagOverFirst()
    {
        const auto streams = std::vector<EmbeddedStreamInfo>{
            Sub(0, "eng", false, false),
            Sub(1, "ger", true, false), // DEFAULT-flagged → wins over first
        };
        const auto sel = BuildTracks(streams, {}, -1, "", "", false);
        QCOMPARE(sel.selectedSubId, int64_t{2});
    }

    void PreferForcedFallsThroughWhenNoForcedSubExists()
    {
        const auto sel = BuildTracks({Sub(0, "eng")}, {}, -1, "", "", true);
        QCOMPARE(sel.selectedSubId, int64_t{1}); // plain first-sub fallback
    }

    void ExternalSidecarsGetContainerIndexAndLabel()
    {
        const std::vector<ExternalSource> ext = {
            {"/path/movie.en.srt", false},
            {"/path/movie.mka", true},
        };
        const auto sel = BuildTracks({Audio(0)}, ext, 0, "", "", false);
        QCOMPARE(sel.tracks.size(), std::size_t{3});
        const TrackEntry& sub = sel.tracks[1];
        QVERIFY(sub.external);
        QCOMPARE(sub.kind, TrackKind::Subtitle);
        QCOMPARE(sub.container, 1); // externals index 0 → container 1
        QCOMPARE(sub.streamIndex, -1);
        QCOMPARE(sub.label, std::string("movie.en.srt"));
        QCOMPARE(sel.tracks[2].container, 2);
        QCOMPARE(sel.tracks[2].kind, TrackKind::Audio);
    }

    void ExternalSubtitleIsLastResortFallback()
    {
        // No embedded subs at all → the external sidecar becomes the selection.
        const auto sel = BuildTracks({Audio(0)}, {{"/path/movie.srt", false}}, 0, "", "", false);
        QCOMPARE(sel.selectedSubId, int64_t{2});
        QVERIFY(ById(sel, 2)->selected);

        // But any embedded sub still beats it.
        const auto sel2 = BuildTracks({Audio(0), Sub(1)}, {{"/path/movie.srt", false}}, 0, "", "", false);
        QCOMPARE(sel2.selectedSubId, int64_t{2}); // the embedded Sub(1), id 2
        QVERIFY(!ById(sel2, 3)->selected);
    }

    void NoTracksSelectsNothing()
    {
        const auto sel = BuildTracks({}, {}, -1, "fi", "fi", true);
        QVERIFY(sel.tracks.empty());
        QCOMPARE(sel.selectedAudioId, int64_t{-1});
        QCOMPARE(sel.selectedSubId, int64_t{-1});
    }

    void LangMatchIsCaseInsensitiveAndPrefixTolerant()
    {
        QVERIFY(TrackLangMatches("fi", "FIN"));
        QVERIFY(TrackLangMatches("eng", "en"));
        QVERIFY(!TrackLangMatches("fi", "eng"));
        QVERIFY(!TrackLangMatches("", "eng"));
        QVERIFY(!TrackLangMatches("fi", ""));
    }
};

namespace
{
const ::framelift::test::Registrar<FFmpegTrackSelectTests> kRegisterFFmpegTrackSelectTests{"FFmpegTrackSelectTests"};
}

#include "FFmpegTrackSelectTests.moc"
