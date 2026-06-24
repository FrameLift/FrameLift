#include "Settings.h"
#include "TempIni.h"

#include "AudioSettings.h"
#include "CacheSettings.h"
#include "CoreSettings.h"
#include "GraphicsSettings.h"
#include "PlaybackSettings.h"
#include "SubtitleSettings.h"
#include "ThemeSettings.h"
#include "UiSettings.h"

#include <gtest/gtest.h>
#include <cstddef>
#include <iterator>
#include <string>

// Covers default values, the Load() parser, and the synchronous Save() merge
// (round-trip + preservation of plugin-owned sections/keys).

TEST(SettingsTest, DefaultsAreSane)
{
    const Settings s;
    EXPECT_FLOAT_EQ(s.Get<GeneralSettings>().maxDisplayRatio, 0.8f);
    EXPECT_TRUE(s.Get<PlaybackSettings>().hwdec);
    EXPECT_EQ(s.Get<PlaybackSettings>().hwdecMode, "auto");
    EXPECT_FLOAT_EQ(s.Get<UiSettings>().panelWidth, 320.f);
    EXPECT_EQ(s.Get<AudioSettings>().dynaudnormFrameLen, 100);
    EXPECT_EQ(s.Get<FilesSettings>().videoExtensions.rfind("mp4", 0), 0u); // starts with "mp4"
}

TEST(SettingsTest, ThemeDefaults)
{
    const Settings s;
    EXPECT_EQ(s.Get<ThemeSettings>().preset, "dark");
    EXPECT_EQ(s.Get<ThemeSettings>().accentColor, "#4296FA");
    EXPECT_TRUE(s.Get<ThemeSettings>().fontFile.empty());
    EXPECT_FLOAT_EQ(s.Get<ThemeSettings>().fontSize, 16.0f);
}

TEST(SettingsTest, ThemeLoadSaveRoundTrip)
{
    const char* content = R"([theme]
preset=light
accentColor=#AABBCC
fontFile=/fonts/Roboto.ttf
fontSize=18
)";
    const TempFile f(content);

    Settings s;
    s.Load(f.str());
    EXPECT_EQ(s.Get<ThemeSettings>().preset, "light");
    EXPECT_EQ(s.Get<ThemeSettings>().accentColor, "#AABBCC");
    EXPECT_EQ(s.Get<ThemeSettings>().fontFile, "/fonts/Roboto.ttf");
    EXPECT_FLOAT_EQ(s.Get<ThemeSettings>().fontSize, 18.f);

    // Round-trip: Save then Load into a fresh Settings.
    const TempFile out;
    s.Save(out.str());
    Settings s2;
    s2.Load(out.str());
    EXPECT_EQ(s2.Get<ThemeSettings>().preset, "light");
    EXPECT_EQ(s2.Get<ThemeSettings>().accentColor, "#AABBCC");
    EXPECT_EQ(s2.Get<ThemeSettings>().fontFile, "/fonts/Roboto.ttf");
    EXPECT_FLOAT_EQ(s2.Get<ThemeSettings>().fontSize, 18.f);
}

TEST(SettingsTest, LoadOverridesFields)
{
    const char* content = R"([general]
maxDisplayRatio=0.5
[playback]
hwdec=0
[ui]
panelWidth=500
[files]
videoExtensions=avi;mov
[audio]
dynaudnormFrameLen=250
)";
    const TempFile f(content);

    Settings s;
    s.Load(f.str());

    EXPECT_FLOAT_EQ(s.Get<GeneralSettings>().maxDisplayRatio, 0.5f);
    EXPECT_FALSE(s.Get<PlaybackSettings>().hwdec);
    EXPECT_EQ(s.Get<PlaybackSettings>().hwdecMode, "off");
    EXPECT_FLOAT_EQ(s.Get<UiSettings>().panelWidth, 500.f);
    EXPECT_EQ(s.Get<FilesSettings>().videoExtensions, "avi;mov");
    EXPECT_EQ(s.Get<AudioSettings>().dynaudnormFrameLen, 250);
}

TEST(SettingsTest, ReadAheadCacheDefaults)
{
    const Settings s;
    EXPECT_TRUE(s.Get<CacheSettings>().readAheadEnabled);
    EXPECT_EQ(s.Get<CacheSettings>().readAheadSizeMB, 64);
}

TEST(SettingsTest, ReadAheadCacheLoadAndRoundTrip)
{
    const TempFile f("[cache]\nreadAheadEnabled=0\nreadAheadSizeMB=256\n");

    Settings s;
    s.Load(f.str());
    EXPECT_FALSE(s.Get<CacheSettings>().readAheadEnabled);
    EXPECT_EQ(s.Get<CacheSettings>().readAheadSizeMB, 256);

    const TempFile out;
    s.Save(out.str());
    Settings s2;
    s2.Load(out.str());
    EXPECT_FALSE(s2.Get<CacheSettings>().readAheadEnabled);
    EXPECT_EQ(s2.Get<CacheSettings>().readAheadSizeMB, 256);
}

TEST(SettingsTest, AudioPreferencesLoadAndRoundTrip)
{
    const TempFile f("[audio]\ndefaultLanguage=jpn\noutputDevice=Headphones\ndefaultVolume=72\nsyncOffsetMs=-125\n"
                     "channelMode=2\nduckingEnabled=1\nduckingLevel=35\nnormalizeEnabled=1\n");

    Settings s;
    s.Load(f.str());
    EXPECT_EQ(s.Get<AudioSettings>().defaultLanguage, "jpn");
    EXPECT_EQ(s.Get<AudioSettings>().outputDevice, "Headphones");
    EXPECT_EQ(s.Get<AudioSettings>().defaultVolume, 72);
    EXPECT_EQ(s.Get<AudioSettings>().syncOffsetMs, -125);
    EXPECT_EQ(s.Get<AudioSettings>().channelMode, 2);
    EXPECT_TRUE(s.Get<AudioSettings>().duckingEnabled);
    EXPECT_EQ(s.Get<AudioSettings>().duckingLevel, 35);
    EXPECT_TRUE(s.Get<AudioSettings>().normalizeEnabled);

    const TempFile out;
    s.Save(out.str());
    Settings s2;
    s2.Load(out.str());
    EXPECT_EQ(s2.Get<AudioSettings>().defaultLanguage, "jpn");
    EXPECT_EQ(s2.Get<AudioSettings>().outputDevice, "Headphones");
    EXPECT_EQ(s2.Get<AudioSettings>().defaultVolume, 72);
    EXPECT_EQ(s2.Get<AudioSettings>().syncOffsetMs, -125);
    EXPECT_EQ(s2.Get<AudioSettings>().channelMode, 2);
    EXPECT_TRUE(s2.Get<AudioSettings>().duckingEnabled);
    EXPECT_EQ(s2.Get<AudioSettings>().duckingLevel, 35);
    EXPECT_TRUE(s2.Get<AudioSettings>().normalizeEnabled);
}

TEST(SettingsTest, MissingKeysKeepDefaults)
{
    const TempFile f("[ui]\npanelWidth=400\n");

    Settings s;
    s.Load(f.str());

    EXPECT_FLOAT_EQ(s.Get<UiSettings>().panelWidth, 400.f); // overridden
    EXPECT_TRUE(s.Get<PlaybackSettings>().hwdec); // untouched default
    EXPECT_FLOAT_EQ(s.Get<GeneralSettings>().maxDisplayRatio, 0.8f); // untouched default
}

TEST(SettingsTest, UnknownKeysAndSectionsIgnored)
{
    const TempFile f("[ui]\npanelWidth=350\nbogusKey=123\n[nosuchsection]\nfoo=bar\n");

    Settings s;
    s.Load(f.str());

    EXPECT_FLOAT_EQ(s.Get<UiSettings>().panelWidth, 350.f);
    // No crash, other fields unaffected.
    EXPECT_TRUE(s.Get<PlaybackSettings>().hwdec);
}

TEST(SettingsTest, MalformedNumericValueIsIgnored)
{
    const TempFile f("[ui]\npanelWidth=notanumber\n");

    Settings s;
    s.Load(f.str());

    // std::stof throws → caught → field keeps its default.
    EXPECT_FLOAT_EQ(s.Get<UiSettings>().panelWidth, 320.f);
}

// ── Incorrect setting type handling (issue #2) ──────────────────────────────────
// Settings are stringly-typed; Load() parses each typed field and swallows any
// parse exception so a wrong-typed value never crashes and never corrupts a field
// — it simply keeps its compiled-in default. These tests pin that behavior.

TEST(SettingsTest, IntFieldRejectsNonNumeric)
{
    const TempFile f("[cache]\nreadAheadSizeMB=abc\n");

    Settings s;
    s.Load(f.str());

    // std::stoi throws std::invalid_argument → caught → field keeps its default.
    EXPECT_EQ(s.Get<CacheSettings>().readAheadSizeMB, 64);
}

TEST(SettingsTest, IntFieldOutOfRangeKeepsDefault)
{
    const TempFile f("[cache]\nreadAheadSizeMB=999999999999\n");

    Settings s;
    s.Load(f.str());

    // std::stoi throws std::out_of_range → caught → field keeps its default.
    EXPECT_EQ(s.Get<CacheSettings>().readAheadSizeMB, 64);
}

TEST(SettingsTest, IntFieldFromFloatStringTruncates)
{
    const TempFile f("[cache]\nreadAheadSizeMB=3.14\n");

    Settings s;
    s.Load(f.str());

    // std::stoi parses the leading integer prefix ("3") and stops — no crash,
    // consistent: a float written to an int field truncates rather than throwing.
    EXPECT_EQ(s.Get<CacheSettings>().readAheadSizeMB, 3);
}

TEST(SettingsTest, BoolFieldOnlyOneIsTrue)
{
    // Bool fields parse via (v == "1"); every other token is false, none throw.
    const Settings def;
    EXPECT_TRUE(def.Get<PlaybackSettings>().hwdec); // default is true, so "not 1" must flip it to false

    for (const char* token : {"true", "2", "yes"})
    {
        const TempFile f(std::string("[playback]\nhwdec=") + token + "\n");
        Settings s;
        s.Load(f.str());
        EXPECT_FALSE(s.Get<PlaybackSettings>().hwdec) << "token=" << token;
    }

    const TempFile one("[playback]\nhwdec=1\n");
    Settings s;
    s.Load(one.str());
    EXPECT_TRUE(s.Get<PlaybackSettings>().hwdec);
}

TEST(SettingsTest, HwdecModeOverridesLegacyBool)
{
    const TempFile f("[playback]\nhwdec=0\nhwdecMode=cuda\n");

    Settings s;
    s.Load(f.str());

    EXPECT_TRUE(s.Get<PlaybackSettings>().hwdec);
    EXPECT_EQ(s.Get<PlaybackSettings>().hwdecMode, "cuda");
}

TEST(SettingsTest, InvalidHwdecModeNormalizesToAuto)
{
    const TempFile f("[playback]\nhwdecMode=definitely-not-a-mode\n");

    Settings s;
    s.Load(f.str());

    EXPECT_TRUE(s.Get<PlaybackSettings>().hwdec);
    EXPECT_EQ(s.Get<PlaybackSettings>().hwdecMode, "auto");
}

TEST(SettingsTest, SaveSynchronizesLegacyHwdecFromMode)
{
    const TempFile f;
    Settings s;
    s.Get<PlaybackSettings>().hwdec = true;
    s.Get<PlaybackSettings>().hwdecMode = "off";
    s.Save(f.str());

    Settings loaded;
    loaded.Load(f.str());

    EXPECT_FALSE(loaded.Get<PlaybackSettings>().hwdec);
    EXPECT_EQ(loaded.Get<PlaybackSettings>().hwdecMode, "off");
}

TEST(SettingsTest, EmptyValueKeepsDefault)
{
    const TempFile f("[ui]\npanelWidth=\n");

    Settings s;
    s.Load(f.str());

    // std::stof("") throws → caught → field keeps its default.
    EXPECT_FLOAT_EQ(s.Get<UiSettings>().panelWidth, 320.f);
}

TEST(SettingsTest, MissingFileLeavesDefaults)
{
    // Load() of a missing file writes a default file; the in-memory object
    // must still hold defaults.
    Settings s;
    const auto missing = UniqueTempPath();
    s.Load(missing.string());
    EXPECT_FLOAT_EQ(s.Get<UiSettings>().panelWidth, 320.f);
    EXPECT_TRUE(std::filesystem::exists(missing)); // Save() is synchronous now
    std::error_code ec;
    std::filesystem::remove(missing, ec);
}

TEST(SettingsTest, EmptyFileSeedsDefaults)
{
    // An existing but empty settings.ini must be seeded with defaults on Load,
    // exactly like a missing file — not left blank.
    const TempFile f(""); // 0-byte file that exists
    ASSERT_TRUE(std::filesystem::exists(f.path));
    ASSERT_EQ(std::filesystem::file_size(f.path), 0u);

    Settings s;
    s.Load(f.str());

    EXPECT_FLOAT_EQ(s.Get<UiSettings>().panelWidth, 320.f); // in-memory defaults intact
    EXPECT_GT(std::filesystem::file_size(f.path), 0u); // defaults written back to disk

    // Re-loading the now-populated file yields the same defaults.
    Settings reloaded;
    reloaded.Load(f.str());
    EXPECT_FLOAT_EQ(reloaded.Get<UiSettings>().panelWidth, 320.f);
}

TEST(SettingsTest, SaveLoadRoundTrip)
{
    const TempFile f;

    Settings s;
    s.Get<GeneralSettings>().maxDisplayRatio = 0.65f;
    s.Get<PlaybackSettings>().hwdec = false;
    s.Get<PlaybackSettings>().hwdecMode = "off";
    s.Get<UiSettings>().panelWidth = 444.f;
    s.Get<FilesSettings>().videoExtensions = "mkv;webm";
    s.Get<AudioSettings>().dynaudnormFrameLen = 321;
    s.Get<KeybindSettings>().togglePause = "P";
    s.Save(f.str());

    Settings loaded;
    loaded.Load(f.str());

    EXPECT_FLOAT_EQ(loaded.Get<GeneralSettings>().maxDisplayRatio, 0.65f);
    EXPECT_FALSE(loaded.Get<PlaybackSettings>().hwdec);
    EXPECT_EQ(loaded.Get<PlaybackSettings>().hwdecMode, "off");
    EXPECT_FLOAT_EQ(loaded.Get<UiSettings>().panelWidth, 444.f);
    EXPECT_EQ(loaded.Get<FilesSettings>().videoExtensions, "mkv;webm");
    EXPECT_EQ(loaded.Get<AudioSettings>().dynaudnormFrameLen, 321);
    EXPECT_EQ(loaded.Get<KeybindSettings>().togglePause, "P");
}

TEST(SettingsTest, SavePreservesUnknownSectionsAndKeys)
{
    // The merge-save preserves unknown sections and any unknown keys inside an
    // owned section, while updating owned keys in place.
    const TempFile f("[Playlist]\nscanSubdirs=1\n\n[keybinds]\ntogglePause=Space\nhandAddedKey=Ctrl+J\n");

    Settings s;
    s.Get<KeybindSettings>().togglePause = "P";
    s.Save(f.str());

    std::ifstream in(f.str());
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    EXPECT_NE(text.find("[Playlist]"), std::string::npos);
    EXPECT_NE(text.find("scanSubdirs=1"), std::string::npos);
    EXPECT_NE(text.find("handAddedKey=Ctrl+J"), std::string::npos); // unknown key preserved
    EXPECT_NE(text.find("togglePause=P"), std::string::npos);
    EXPECT_EQ(text.find("togglePause=Space"), std::string::npos); // owned key updated
}

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

TEST(SettingsTest, CommentsWrittenForKnownSettings)
{
    const TempFile f;
    Settings s;
    s.Save(f.str());

    const std::string text = ReadAll(f.str());

    // A documentation comment is emitted immediately above its key.
    EXPECT_NE(text.find("# Enable hardware video decoding.\nhwdec="), std::string::npos);
}

TEST(SettingsTest, CommentsAreIdempotent)
{
    const TempFile f;
    Settings s;
    s.Save(f.str());
    const std::string first = ReadAll(f.str());

    s.Save(f.str());
    const std::string second = ReadAll(f.str());

    // Re-saving must not duplicate comments and must be byte-identical.
    EXPECT_EQ(first, second);
    EXPECT_EQ(Count(second, "# Enable hardware video decoding."), 1u);
}

TEST(SettingsTest, UnknownKeysGetNoComments)
{
    // An unknown key hand-added to an owned section must be preserved verbatim and
    // must not receive a host-generated documentation comment.
    const TempFile f("[keybinds]\ntogglePause=Space\nhandAddedKey=Ctrl+J\n");

    Settings s;
    s.Save(f.str());

    const std::string text = ReadAll(f.str());

    const auto pos = text.find("handAddedKey=Ctrl+J");
    ASSERT_NE(pos, std::string::npos); // unknown key preserved
    ASSERT_GT(pos, 0u);
    ASSERT_EQ(text[pos - 1], '\n'); // key sits at the start of its own line

    // The line directly above the unknown key must not be a generated comment.
    const auto prevEnd = pos - 1; // index of the '\n' terminating the previous line
    const auto prevStart = (prevEnd == 0) ? std::string::npos : text.rfind('\n', prevEnd - 1);
    const auto from = (prevStart == std::string::npos) ? 0 : prevStart + 1;
    const std::string prevLine = text.substr(from, prevEnd - from);
    EXPECT_NE(prevLine.rfind("# ", 0), 0u); // previous line is not a "# ..." comment
}
