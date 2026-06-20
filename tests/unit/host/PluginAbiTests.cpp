#include <framelift/PluginABI.h>

#include <gtest/gtest.h>

TEST(PluginAbiTest, SameMajorMinorLoads)
{
    EXPECT_TRUE(FrameLiftAbiCompatible(3, 0, 3, 0));
}

TEST(PluginAbiTest, OlderPluginMinorLoads)
{
    EXPECT_TRUE(FrameLiftAbiCompatible(3, 0, 3, 2));
}

TEST(PluginAbiTest, NewerPluginMinorRejected)
{
    EXPECT_FALSE(FrameLiftAbiCompatible(3, 2, 3, 1));
}

TEST(PluginAbiTest, DifferentMajorRejectedEitherDirection)
{
    EXPECT_FALSE(FrameLiftAbiCompatible(2, 2, 3, 0));
    EXPECT_FALSE(FrameLiftAbiCompatible(3, 0, 2, 9));
}

TEST(PluginAbiTest, PackageMetadataIsAbiThree)
{
    EXPECT_EQ(FRAMELIFT_PLUGIN_ABI_MAJOR, 3);
    EXPECT_EQ(FRAMELIFT_PLUGIN_ABI_MINOR, 1);
}

TEST(PluginAbiTest, MetadataCarriesPackageAndModules)
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
    static constexpr FrameLiftPluginInfo info{FRAMELIFT_PLUGIN_ABI_MAJOR,
                                              FRAMELIFT_PLUGIN_ABI_MINOR,
                                              FRAMELIFT_PLUGIN_ABI_PATCH,
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
