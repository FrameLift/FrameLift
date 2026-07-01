#include "Settings.h"
#include "TempIni.h"

#include "AudioSettings.h"
#include "CacheSettings.h"
#include "CoreSettings.h"
#include "PlaybackSettings.h"
#include "SubtitleSettings.h"
#include "ThemeSettings.h"
#include "UISettings.h"

#include "QtTestRunner.h"

#include <QtTest/QtTest>
#include <cstddef>
#include <iterator>
#include <string>

// Covers default values, the Load() parser, and the synchronous Save() merge
// (round-trip + preservation of plugin-owned sections/keys).
namespace
{
std::string ReadAll(const std::string& path)
{
    std::ifstream in(path);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// Count non-overlapping occurrences of needle in haystack.
std::size_t Count(const std::string& haystack, const std::string& needle)
{
    std::size_t n = 0;
    for (auto pos = haystack.find(needle); pos != std::string::npos; pos = haystack.find(needle, pos + needle.size()))
    {
        ++n;
    }
    return n;
}
} // namespace

class SettingsTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void DefaultsAreSane()
    {
        const Settings s;
        QVERIFY(s.Get<PlaybackSettings>().hwdec);
        QVERIFY((s.Get<PlaybackSettings>().hwdecMode) == ("auto"));
        QCOMPARE(s.Get<UISettings>().panelWidth, 320.f);
        QVERIFY((s.Get<AudioSettings>().dynaudnormFrameLen) == (100));
        QVERIFY((s.Get<FilesSettings>().videoExtensions.rfind("mp4", 0)) == (0u)); // starts with "mp4"
    }

    void ThemeDefaults()
    {
        const Settings s;
        QVERIFY((s.Get<ThemeSettings>().preset) == ("dark"));
        QVERIFY((s.Get<ThemeSettings>().accentColor) == ("#4296FA"));
    }

    void ThemeLoadSaveRoundTrip()
    {
        const char* content = R"([theme]
preset=light
accentColor=#AABBCC
)";
        const TempFile f(content);

        Settings s;
        s.Load(f.str());
        QVERIFY((s.Get<ThemeSettings>().preset) == ("light"));
        QVERIFY((s.Get<ThemeSettings>().accentColor) == ("#AABBCC"));

        // Round-trip: Save then Load into a fresh Settings.
        const TempFile out;
        s.Save(out.str());
        Settings s2;
        s2.Load(out.str());
        QVERIFY((s2.Get<ThemeSettings>().preset) == ("light"));
        QVERIFY((s2.Get<ThemeSettings>().accentColor) == ("#AABBCC"));
    }

    void LoadOverridesFields()
    {
        const char* content = R"([playback]
hwdec=0
[ui]
panelWidth=500
[files]
videoExtensions="avi;mov"
[audio]
dynaudnormFrameLen=250
)";
        const TempFile f(content);

        Settings s;
        s.Load(f.str());

        QVERIFY(!(s.Get<PlaybackSettings>().hwdec));
        QVERIFY((s.Get<PlaybackSettings>().hwdecMode) == ("off"));
        QCOMPARE(s.Get<UISettings>().panelWidth, 500.f);
        QVERIFY((s.Get<FilesSettings>().videoExtensions) == ("avi;mov"));
        QVERIFY((s.Get<AudioSettings>().dynaudnormFrameLen) == (250));
    }

    void ReadAheadCacheDefaults()
    {
        const Settings s;
        QVERIFY(s.Get<CacheSettings>().readAheadEnabled);
        QVERIFY((s.Get<CacheSettings>().readAheadSizeMB) == (64));
    }

    void ReadAheadCacheLoadAndRoundTrip()
    {
        const TempFile f("[cache]\nreadAheadEnabled=0\nreadAheadSizeMB=256\n");

        Settings s;
        s.Load(f.str());
        QVERIFY(!(s.Get<CacheSettings>().readAheadEnabled));
        QVERIFY((s.Get<CacheSettings>().readAheadSizeMB) == (256));

        const TempFile out;
        s.Save(out.str());
        Settings s2;
        s2.Load(out.str());
        QVERIFY(!(s2.Get<CacheSettings>().readAheadEnabled));
        QVERIFY((s2.Get<CacheSettings>().readAheadSizeMB) == (256));
    }

    void AudioPreferencesLoadAndRoundTrip()
    {
        const TempFile f(
            "[audio]\ndefaultLanguage=jpn\noutputDevice=Headphones\ndefaultVolume=72\nsyncOffsetMs=-125\n"
            "channelMode=2\nduckingEnabled=1\nduckingLevel=35\nnormalizeEnabled=1\n"
        );

        Settings s;
        s.Load(f.str());
        QVERIFY((s.Get<AudioSettings>().defaultLanguage) == ("jpn"));
        QVERIFY((s.Get<AudioSettings>().outputDevice) == ("Headphones"));
        QVERIFY((s.Get<AudioSettings>().defaultVolume) == (72));
        QVERIFY((s.Get<AudioSettings>().syncOffsetMs) == (-125));
        QVERIFY((s.Get<AudioSettings>().channelMode) == (2));
        QVERIFY(s.Get<AudioSettings>().duckingEnabled);
        QVERIFY((s.Get<AudioSettings>().duckingLevel) == (35));
        QVERIFY(s.Get<AudioSettings>().normalizeEnabled);

        const TempFile out;
        s.Save(out.str());
        Settings s2;
        s2.Load(out.str());
        QVERIFY((s2.Get<AudioSettings>().defaultLanguage) == ("jpn"));
        QVERIFY((s2.Get<AudioSettings>().outputDevice) == ("Headphones"));
        QVERIFY((s2.Get<AudioSettings>().defaultVolume) == (72));
        QVERIFY((s2.Get<AudioSettings>().syncOffsetMs) == (-125));
        QVERIFY((s2.Get<AudioSettings>().channelMode) == (2));
        QVERIFY(s2.Get<AudioSettings>().duckingEnabled);
        QVERIFY((s2.Get<AudioSettings>().duckingLevel) == (35));
        QVERIFY(s2.Get<AudioSettings>().normalizeEnabled);
    }

    void MissingKeysKeepDefaults()
    {
        const TempFile f("[ui]\npanelWidth=400\n");

        Settings s;
        s.Load(f.str());

        QCOMPARE(s.Get<UISettings>().panelWidth, 400.f);                           // overridden
        QVERIFY(s.Get<PlaybackSettings>().hwdec);                                  // untouched default
        QVERIFY((s.Get<FilesSettings>().videoExtensions.rfind("mp4", 0)) == (0u)); // untouched default
    }

    void UnknownKeysAndSectionsIgnored()
    {
        const TempFile f("[ui]\npanelWidth=350\nbogusKey=123\n[nosuchsection]\nfoo=bar\n");

        Settings s;
        s.Load(f.str());

        QCOMPARE(s.Get<UISettings>().panelWidth, 350.f);
        // No crash, other fields unaffected.
        QVERIFY(s.Get<PlaybackSettings>().hwdec);
    }

    void MalformedNumericValueIsIgnored()
    {
        const TempFile f("[ui]\npanelWidth=notanumber\n");

        Settings s;
        s.Load(f.str());

        // std::stof throws → caught → field keeps its default.
        QCOMPARE(s.Get<UISettings>().panelWidth, 320.f);
    }

    // ── Incorrect setting type handling (issue #2) ──────────────────────────────────
    // Settings are stringly-typed; Load() parses each typed field and swallows any
    // parse exception so a wrong-typed value never crashes and never corrupts a field
    // — it simply keeps its compiled-in default. These tests pin that behavior.

    void IntFieldRejectsNonNumeric()
    {
        const TempFile f("[cache]\nreadAheadSizeMB=abc\n");

        Settings s;
        s.Load(f.str());

        // std::stoi throws std::invalid_argument → caught → field keeps its default.
        QVERIFY((s.Get<CacheSettings>().readAheadSizeMB) == (64));
    }

    void IntFieldOutOfRangeKeepsDefault()
    {
        const TempFile f("[cache]\nreadAheadSizeMB=999999999999\n");

        Settings s;
        s.Load(f.str());

        // std::stoi throws std::out_of_range → caught → field keeps its default.
        QVERIFY((s.Get<CacheSettings>().readAheadSizeMB) == (64));
    }

    void IntFieldFromFloatStringTruncates()
    {
        const TempFile f("[cache]\nreadAheadSizeMB=3.14\n");

        Settings s;
        s.Load(f.str());

        // std::stoi parses the leading integer prefix ("3") and stops — no crash,
        // consistent: a float written to an int field truncates rather than throwing.
        QVERIFY((s.Get<CacheSettings>().readAheadSizeMB) == (3));
    }

    void BoolFieldOnlyOneIsTrue()
    {
        // Bool fields parse via (v == "1"); every other token is false, none throw.
        const Settings def;
        QVERIFY(def.Get<PlaybackSettings>().hwdec); // default is true, so "not 1" must flip it to false

        for (const char* token : {"true", "2", "yes"})
        {
            const TempFile f(std::string("[playback]\nhwdec=") + token + "\n");
            Settings s;
            s.Load(f.str());
            QVERIFY2(!(s.Get<PlaybackSettings>().hwdec), (std::string("token=") + token).c_str());
        }

        const TempFile one("[playback]\nhwdec=1\n");
        Settings s;
        s.Load(one.str());
        QVERIFY(s.Get<PlaybackSettings>().hwdec);
    }

    void HwdecModeOverridesLegacyBool()
    {
        const TempFile f("[playback]\nhwdec=0\nhwdecMode=cuda\n");

        Settings s;
        s.Load(f.str());

        QVERIFY(s.Get<PlaybackSettings>().hwdec);
        QVERIFY((s.Get<PlaybackSettings>().hwdecMode) == ("cuda"));
    }

    void InvalidHwdecModeNormalizesToAuto()
    {
        const TempFile f("[playback]\nhwdecMode=definitely-not-a-mode\n");

        Settings s;
        s.Load(f.str());

        QVERIFY(s.Get<PlaybackSettings>().hwdec);
        QVERIFY((s.Get<PlaybackSettings>().hwdecMode) == ("auto"));
    }

    void SaveSynchronizesLegacyHwdecFromMode()
    {
        const TempFile f;
        Settings s;
        s.Get<PlaybackSettings>().hwdec = true;
        s.Get<PlaybackSettings>().hwdecMode = "off";
        s.Save(f.str());

        Settings loaded;
        loaded.Load(f.str());

        QVERIFY(!(loaded.Get<PlaybackSettings>().hwdec));
        QVERIFY((loaded.Get<PlaybackSettings>().hwdecMode) == ("off"));
    }

    void EmptyValueKeepsDefault()
    {
        const TempFile f("[ui]\npanelWidth=\n");

        Settings s;
        s.Load(f.str());

        // std::stof("") throws → caught → field keeps its default.
        QCOMPARE(s.Get<UISettings>().panelWidth, 320.f);
    }

    void MissingFileLeavesDefaults()
    {
        // Load() of a missing file writes a default file; the in-memory object
        // must still hold defaults.
        Settings s;
        const auto missing = UniqueTempPath();
        s.Load(missing.string());
        QCOMPARE(s.Get<UISettings>().panelWidth, 320.f);
        QVERIFY(std::filesystem::exists(missing)); // Save() is synchronous now
        std::error_code ec;
        std::filesystem::remove(missing, ec);
    }

    void EmptyFileSeedsDefaults()
    {
        // An existing but empty settings.ini must be seeded with defaults on Load,
        // exactly like a missing file — not left blank.
        const TempFile f(""); // 0-byte file that exists
        QVERIFY(std::filesystem::exists(f.path));
        QVERIFY((std::filesystem::file_size(f.path)) == (0u));

        Settings s;
        s.Load(f.str());

        QCOMPARE(s.Get<UISettings>().panelWidth, 320.f);      // in-memory defaults intact
        QVERIFY((std::filesystem::file_size(f.path)) > (0u)); // defaults written back to disk

        // Re-loading the now-populated file yields the same defaults.
        Settings reloaded;
        reloaded.Load(f.str());
        QCOMPARE(reloaded.Get<UISettings>().panelWidth, 320.f);
    }

    void SaveLoadRoundTrip()
    {
        const TempFile f;

        Settings s;
        s.Get<PlaybackSettings>().hwdec = false;
        s.Get<PlaybackSettings>().hwdecMode = "off";
        s.Get<UISettings>().panelWidth = 444.f;
        s.Get<FilesSettings>().videoExtensions = "mkv;webm";
        s.Get<AudioSettings>().dynaudnormFrameLen = 321;
        s.Get<KeybindSettings>().togglePause = "P";
        s.Save(f.str());

        Settings loaded;
        loaded.Load(f.str());

        QVERIFY(!(loaded.Get<PlaybackSettings>().hwdec));
        QVERIFY((loaded.Get<PlaybackSettings>().hwdecMode) == ("off"));
        QCOMPARE(loaded.Get<UISettings>().panelWidth, 444.f);
        QVERIFY((loaded.Get<FilesSettings>().videoExtensions) == ("mkv;webm"));
        QVERIFY((loaded.Get<AudioSettings>().dynaudnormFrameLen) == (321));
        QVERIFY((loaded.Get<KeybindSettings>().togglePause) == ("P"));
    }

    void SavePreservesUnknownSectionsAndKeys()
    {
        // The merge-save preserves unknown sections and any unknown keys inside an
        // owned section, while updating owned keys in place.
        const TempFile f("[Playlist]\nscanSubdirs=1\n\n[keybinds]\ntogglePause=Space\nhandAddedKey=Ctrl+J\n");

        Settings s;
        s.Get<KeybindSettings>().togglePause = "P";
        s.Save(f.str());

        std::ifstream in(f.str());
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

        QVERIFY((text.find("[Playlist]")) != (std::string::npos));
        QVERIFY((text.find("scanSubdirs=1")) != (std::string::npos));
        QVERIFY((text.find("handAddedKey=Ctrl+J")) != (std::string::npos)); // unknown key preserved
        QVERIFY((text.find("togglePause=P")) != (std::string::npos));
        QVERIFY((text.find("togglePause=Space")) == (std::string::npos)); // owned key updated
    }

    void CommentsWrittenForKnownSettings()
    {
        const TempFile f;
        Settings s;
        s.Save(f.str());

        const std::string text = ReadAll(f.str());

        QVERIFY((text.find("[playback]")) != (std::string::npos));
        QVERIFY((text.find("hwdec=")) != (std::string::npos));
    }

    void CommentsAreIdempotent()
    {
        const TempFile f;
        Settings s;
        s.Save(f.str());
        const std::string first = ReadAll(f.str());

        s.Save(f.str());
        const std::string second = ReadAll(f.str());

        // Re-saving must not duplicate comments and must be byte-identical.
        QVERIFY((first) == (second));
        QVERIFY((Count(second, "hwdec=")) == (1u));
    }

    void UnknownKeysGetNoComments()
    {
        // An unknown key hand-added to an owned section must be preserved verbatim.
        const TempFile f("[keybinds]\ntogglePause=Space\nhandAddedKey=Ctrl+J\n");

        Settings s;
        s.Save(f.str());

        const std::string text = ReadAll(f.str());

        const auto pos = text.find("handAddedKey=Ctrl+J");
        QVERIFY((pos) != (std::string::npos)); // unknown key preserved
        QVERIFY((pos) > (0u));
        QVERIFY((text[pos - 1]) == ('\n')); // key sits at the start of its own line

        QVERIFY((Count(text, "handAddedKey=Ctrl+J")) == (1u));
    }
};

namespace
{
const ::framelift::test::Registrar<SettingsTest> kRegisterSettingsTest{"SettingsTest"};
}

#include "SettingsTests.moc"
