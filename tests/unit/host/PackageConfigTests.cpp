#include "PackageConfig.h"
#include "TempIni.h"

#include <fstream>
#include <gtest/gtest.h>
#include <iterator>
#include <string>

TEST(PackageConfigTest, AbsentIdDefaultsToEnabled)
{
    const PackageConfig cfg; // nothing loaded
    EXPECT_TRUE(cfg.IsEnabled("framelift.anything"));
    EXPECT_TRUE(cfg.DisabledIds().empty());
}

TEST(PackageConfigTest, MissingFileLeavesEverythingEnabled)
{
    PackageConfig cfg;
    cfg.Load(UniqueTempPath().string()); // file does not exist
    EXPECT_TRUE(cfg.IsEnabled("framelift.overlay"));
    EXPECT_TRUE(cfg.DisabledIds().empty());
}

TEST(PackageConfigTest, LoadParsesRowsAndDefaultsUnlisted)
{
    const TempFile f("# header\nframelift.overlay=disabled\nframelift.playlist=enabled\n");

    PackageConfig cfg;
    cfg.Load(f.str());

    EXPECT_FALSE(cfg.IsEnabled("framelift.overlay"));
    EXPECT_TRUE(cfg.IsEnabled("framelift.playlist"));
    EXPECT_TRUE(cfg.IsEnabled("framelift.history")); // unlisted ⇒ enabled

    const auto disabled = cfg.DisabledIds();
    EXPECT_EQ(disabled.size(), 1u);
    EXPECT_TRUE(disabled.contains("framelift.overlay"));
}

TEST(PackageConfigTest, SetAndSaveRoundTrip)
{
    const TempFile f;
    {
        PackageConfig cfg;
        cfg.Set("framelift.overlay", false);
        cfg.Set("framelift.playlist", true);
        cfg.Save(f.str());
    }

    PackageConfig reloaded;
    reloaded.Load(f.str());
    EXPECT_FALSE(reloaded.IsEnabled("framelift.overlay"));
    EXPECT_TRUE(reloaded.IsEnabled("framelift.playlist"));
}

TEST(PackageConfigTest, EnsureKnownAddsAsEnabledWithoutOverriding)
{
    PackageConfig cfg;
    cfg.Set("framelift.overlay", false);
    cfg.EnsureKnown({"framelift.overlay", "framelift.history"});

    EXPECT_FALSE(cfg.IsEnabled("framelift.overlay")); // existing state preserved
    EXPECT_TRUE(cfg.IsEnabled("framelift.history"));  // newly known ⇒ enabled
}

TEST(PackageConfigTest, SaveWritesSortedRowsWithHeader)
{
    const TempFile f;
    PackageConfig cfg;
    cfg.Set("framelift.zzz", true);
    cfg.Set("framelift.aaa", false);
    cfg.Save(f.str());

    std::ifstream in(f.str());
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    EXPECT_EQ(text.rfind("# FrameLift module enablement", 0), 0u); // starts with the header
    const auto aaa = text.find("framelift.aaa=disabled");
    const auto zzz = text.find("framelift.zzz=enabled");
    ASSERT_NE(aaa, std::string::npos);
    ASSERT_NE(zzz, std::string::npos);
    EXPECT_LT(aaa, zzz); // sorted by id
}
