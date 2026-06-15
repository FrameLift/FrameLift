#include "PluginSettingsImpl.h"
#include "TempIni.h"

#include <gtest/gtest.h>
#include <fstream>
#include <iterator>
#include <string>

TEST(PluginSettingsTest, MissingSectionReturnsDefaults)
{
    const TempFile f; // empty, non-existent section
    const PluginSettingsImpl ps("MyPlugin", f.str());

    EXPECT_FALSE(ps.WasLoaded());
    EXPECT_EQ(ps.KeyCount(), 0);
    EXPECT_STREQ(ps.GetString("name", "fallback"), "fallback");
    EXPECT_EQ(ps.GetInt("count", 7), 7);
    EXPECT_FLOAT_EQ(ps.GetFloat("ratio", 2.5f), 2.5f);
    EXPECT_TRUE(ps.GetBool("flag", true));
}

TEST(PluginSettingsTest, SaveThenReloadRoundTrips)
{
    const TempFile f;
    {
        PluginSettingsImpl ps("MyPlugin", f.str());
        ps.SetString("name", "FrameLift");
        ps.SetInt("count", 42);
        ps.SetBool("flag", true);
        ps.SetFloat("ratio", 1.5f);
        EXPECT_EQ(ps.KeyCount(), 4);
        ps.Save();
    }

    const PluginSettingsImpl reloaded("MyPlugin", f.str());
    EXPECT_TRUE(reloaded.WasLoaded());
    EXPECT_EQ(reloaded.KeyCount(), 4);
    EXPECT_STREQ(reloaded.GetString("name"), "FrameLift");
    EXPECT_EQ(reloaded.GetInt("count"), 42);
    EXPECT_TRUE(reloaded.GetBool("flag"));
    EXPECT_FLOAT_EQ(reloaded.GetFloat("ratio"), 1.5f);
}

TEST(PluginSettingsTest, SectionsAreIsolated)
{
    const TempFile f;
    {
        PluginSettingsImpl a("SectionA", f.str());
        a.SetString("k", "valueA");
        a.Save();
    }
    {
        PluginSettingsImpl b("SectionB", f.str());
        b.SetString("k", "valueB");
        b.Save();
    }

    const PluginSettingsImpl a("SectionA", f.str());
    const PluginSettingsImpl b("SectionB", f.str());
    EXPECT_STREQ(a.GetString("k"), "valueA");
    EXPECT_STREQ(b.GetString("k"), "valueB");

    // A section that was never written is not "loaded".
    const PluginSettingsImpl c("SectionC", f.str());
    EXPECT_FALSE(c.WasLoaded());
}

TEST(PluginSettingsTest, BadNumericFallsBackToDefault)
{
    const TempFile f("[MyPlugin]\ncount=notanumber\n");
    const PluginSettingsImpl ps("MyPlugin", f.str());

    EXPECT_TRUE(ps.WasLoaded());
    EXPECT_EQ(ps.GetInt("count", -1), -1); // stoi throws → default
    EXPECT_FLOAT_EQ(ps.GetFloat("count", -1.f), -1.f);
}

// ── Incorrect setting type handling (issue #2) ──────────────────────────────────
// Values are stored as strings; the typed getters reinterpret them on read. A
// value read as the "wrong" type must never crash — it either converts where it
// can or falls back to the caller's default. These tests pin that behavior.

TEST(PluginSettingsTest, GetIntOutOfRangeFallsBack)
{
    const TempFile f("[MyPlugin]\ncount=999999999999\n");
    const PluginSettingsImpl ps("MyPlugin", f.str());

    // std::stoi throws std::out_of_range → caught → returns caller's default.
    EXPECT_EQ(ps.GetInt("count", -1), -1);
}

TEST(PluginSettingsTest, GetIntFromFloatStringTruncates)
{
    const TempFile f("[MyPlugin]\ncount=3.14\n");
    const PluginSettingsImpl ps("MyPlugin", f.str());

    // std::stoi parses the leading integer prefix and stops — no throw.
    EXPECT_EQ(ps.GetInt("count"), 3);
}

TEST(PluginSettingsTest, GetBoolOnlyOneIsTrue)
{
    for (const char* token : {"true", "0", "2", "yes"})
    {
        const TempFile f(std::string("[MyPlugin]\nflag=") + token + "\n");
        const PluginSettingsImpl ps("MyPlugin", f.str());
        EXPECT_FALSE(ps.GetBool("flag", true)) << "token=" << token;
    }

    const TempFile one("[MyPlugin]\nflag=1\n");
    const PluginSettingsImpl ps("MyPlugin", one.str());
    EXPECT_TRUE(ps.GetBool("flag", false));
}

TEST(PluginSettingsTest, CrossTypeReadsAreSafe)
{
    const TempFile f;
    PluginSettingsImpl ps("MyPlugin", f.str());

    // An int stored, then read as a float.
    ps.SetInt("n", 42);
    EXPECT_FLOAT_EQ(ps.GetFloat("n"), 42.f);

    // A bool serializes as "1"/"0", so reading it back as an int yields 1/0.
    ps.SetBool("b", true);
    EXPECT_EQ(ps.GetInt("b"), 1);
    ps.SetBool("b", false);
    EXPECT_EQ(ps.GetInt("b"), 0);
}

TEST(PluginSettingsTest, EmptyValueFallsBackToDefault)
{
    const TempFile f("[MyPlugin]\nx=\n");
    const PluginSettingsImpl ps("MyPlugin", f.str());

    EXPECT_TRUE(ps.WasLoaded());
    EXPECT_EQ(ps.GetInt("x", -1), -1);             // std::stoi("") throws → default
    EXPECT_FLOAT_EQ(ps.GetFloat("x", -1.f), -1.f); // std::stof("") throws → default
}

// Plugin keybinds live in their own [<Plugin>.keybinds] section, so writing them
// must leave the host-owned, commented [keybinds] section untouched.
TEST(PluginSettingsTest, PluginKeybindSectionLeavesHostKeybindsIntact)
{
    const TempFile f(
        "[keybinds]\n# Key combo to play/pause.\ntogglePause=Space\n# Key combo to quit.\nquit=Ctrl+Q\n"
    );

    {
        PluginSettingsImpl ps("history.keybinds", f.str());
        ps.SetString("toggleHistory", "H");
        ps.Save();
    }

    std::ifstream in(f.str());
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    // The plugin's own camelCase section was created with the bare action key.
    EXPECT_NE(text.find("[history.keybinds]"), std::string::npos);
    EXPECT_NE(text.find("toggleHistory=H"), std::string::npos);

    // The host [keybinds] section — including its comments — survives verbatim.
    EXPECT_NE(text.find("# Key combo to play/pause.\ntogglePause=Space"), std::string::npos);
    EXPECT_NE(text.find("quit=Ctrl+Q"), std::string::npos);

    // Reloading the plugin section yields the written value.
    const PluginSettingsImpl reloaded("history.keybinds", f.str());
    EXPECT_TRUE(reloaded.WasLoaded());
    EXPECT_STREQ(reloaded.GetString("toggleHistory"), "H");
}
