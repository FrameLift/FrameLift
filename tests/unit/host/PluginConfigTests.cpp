#include "PluginConfig.h"
#include "TempIni.h"

#include <fstream>
#include <gtest/gtest.h>
#include <iterator>
#include <string>

TEST(PluginConfigTest, AbsentIdDefaultsToEnabled)
{
    const PluginConfig cfg; // nothing loaded
    EXPECT_TRUE(cfg.IsEnabled("framelift.anything"));
    EXPECT_TRUE(cfg.DisabledIds().empty());
}

TEST(PluginConfigTest, MissingFileLeavesEverythingEnabled)
{
    PluginConfig cfg;
    cfg.Load(UniqueTempPath().string()); // file does not exist
    EXPECT_TRUE(cfg.IsEnabled("framelift.overlay"));
    EXPECT_TRUE(cfg.DisabledIds().empty());
}

TEST(PluginConfigTest, LoadParsesRowsAndDefaultsUnlisted)
{
    const TempFile f("# header\nframelift.overlay=disabled\nframelift.playlist=enabled\n");

    PluginConfig cfg;
    cfg.Load(f.str());

    EXPECT_FALSE(cfg.IsEnabled("framelift.overlay"));
    EXPECT_TRUE(cfg.IsEnabled("framelift.playlist"));
    EXPECT_TRUE(cfg.IsEnabled("framelift.history")); // unlisted ⇒ enabled

    const auto disabled = cfg.DisabledIds();
    EXPECT_EQ(disabled.size(), 1u);
    EXPECT_TRUE(disabled.contains("framelift.overlay"));
}

TEST(PluginConfigTest, SetAndSaveRoundTrip)
{
    const TempFile f;
    {
        PluginConfig cfg;
        cfg.Set("framelift.overlay", false);
        cfg.Set("framelift.playlist", true);
        cfg.Save(f.str());
    }

    PluginConfig reloaded;
    reloaded.Load(f.str());
    EXPECT_FALSE(reloaded.IsEnabled("framelift.overlay"));
    EXPECT_TRUE(reloaded.IsEnabled("framelift.playlist"));
}

TEST(PluginConfigTest, EnsureKnownAddsAsEnabledWithoutOverriding)
{
    PluginConfig cfg;
    cfg.Set("framelift.overlay", false);
    cfg.EnsureKnown({"framelift.overlay", "framelift.history"});

    EXPECT_FALSE(cfg.IsEnabled("framelift.overlay")); // existing state preserved
    EXPECT_TRUE(cfg.IsEnabled("framelift.history"));  // newly known ⇒ enabled
}

TEST(PluginConfigTest, SaveWritesSortedRowsWithHeader)
{
    const TempFile f;
    PluginConfig cfg;
    cfg.Set("framelift.zzz", true);
    cfg.Set("framelift.aaa", false);
    cfg.Save(f.str());

    std::ifstream in(f.str());
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    EXPECT_EQ(text.rfind("# FrameLift plugin enablement", 0), 0u); // starts with the header
    const auto aaa = text.find("framelift.aaa=disabled");
    const auto zzz = text.find("framelift.zzz=enabled");
    ASSERT_NE(aaa, std::string::npos);
    ASSERT_NE(zzz, std::string::npos);
    EXPECT_LT(aaa, zzz); // sorted by id
}
