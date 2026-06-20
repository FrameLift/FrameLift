#include <framelift/ModuleABI.h>

#include <gtest/gtest.h>

TEST(PackageAbiTest, MatchingEpochLoads)
{
    EXPECT_TRUE(FrameLiftAbiCompatible(FRAMELIFT_ABI_VERSION, FRAMELIFT_ABI_VERSION));
}

TEST(PackageAbiTest, MismatchedEpochRejectedEitherDirection)
{
    EXPECT_FALSE(FrameLiftAbiCompatible(FRAMELIFT_ABI_VERSION - 1, FRAMELIFT_ABI_VERSION));
    EXPECT_FALSE(FrameLiftAbiCompatible(FRAMELIFT_ABI_VERSION + 1, FRAMELIFT_ABI_VERSION));
}

TEST(PackageAbiTest, MetadataCarriesPackageAndModules)
{
    static constexpr const char* const kFeatures[] = {"playlist.panel", "playlist.navigation"};
    static constexpr FrameLiftModuleInfo kModules[] = {
        {"framelift.playlist.core",
         "Playlist Core",
         "Directory scanning",
         {kFeatures, 2},
         {nullptr, 0},
         {nullptr, 0},
         {nullptr, 0},
         {nullptr, 0},
         {nullptr, 0}},
    };
    static constexpr FrameLiftPackageInfo info{FRAMELIFT_ABI_VERSION,
                                              "framelift.playlist",
                                              "FrameLift.Playlist.Core",
                                              "Playlist",
                                              {1, 2, 3},
                                              "FrameLift",
                                              "Folder playlist",
                                              kModules,
                                              1};

    EXPECT_STREQ(info.packageId, "framelift.playlist");
    EXPECT_STREQ(info.moduleFile, "FrameLift.Playlist.Core");
    ASSERT_EQ(info.moduleCount, 1);
    EXPECT_STREQ(info.modules[0].id, "framelift.playlist.core");
    ASSERT_EQ(info.modules[0].providesFeatures.count, 2);
    EXPECT_STREQ(info.modules[0].providesFeatures.items[1], "playlist.navigation");
}
