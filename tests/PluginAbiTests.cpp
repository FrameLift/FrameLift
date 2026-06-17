#include <framelift/PluginABI.h>

#include <gtest/gtest.h>

// The host loads a plugin iff plugin.abiMajor == host.abiMajor and
// plugin.abiMinor <= host.abiMinor. Patch never affects the decision.

TEST(PluginAbiTest, SameMajorMinorLoads)
{
    EXPECT_TRUE(FrameLiftAbiCompatible(1, 2, 1, 2));
}

TEST(PluginAbiTest, OlderPluginMinorLoads)
{
    // Plugin built against an older minor than the host — host is a superset.
    EXPECT_TRUE(FrameLiftAbiCompatible(1, 1, 1, 3));
    EXPECT_TRUE(FrameLiftAbiCompatible(2, 0, 2, 5));
}

TEST(PluginAbiTest, NewerPluginMinorRejected)
{
    // Plugin needs host features the older host does not provide.
    EXPECT_FALSE(FrameLiftAbiCompatible(1, 4, 1, 3));
    EXPECT_FALSE(FrameLiftAbiCompatible(1, 1, 1, 0));
}

TEST(PluginAbiTest, DifferentMajorRejectedEitherDirection)
{
    EXPECT_FALSE(FrameLiftAbiCompatible(2, 0, 1, 9));
    EXPECT_FALSE(FrameLiftAbiCompatible(1, 9, 2, 0));
}

TEST(PluginAbiTest, PatchDoesNotAffectDecision)
{
    // FrameLiftAbiCompatible takes no patch argument — patch is informational only.
    // The descriptor still carries it for tooling.
    constexpr FrameLiftPluginInfo info{FRAMELIFT_PLUGIN_ABI_MAJOR, FRAMELIFT_PLUGIN_ABI_MINOR, FRAMELIFT_PLUGIN_ABI_PATCH, "Probe",
                                  {1, 0, 0}};
    EXPECT_EQ(info.abiPatch, FRAMELIFT_PLUGIN_ABI_PATCH);
    EXPECT_TRUE(FrameLiftAbiCompatible(info.abiMajor, info.abiMinor, FRAMELIFT_PLUGIN_ABI_MAJOR, FRAMELIFT_PLUGIN_ABI_MINOR));
}

TEST(PluginAbiTest, AudioPreferencesAreMinorTwo)
{
    EXPECT_EQ(FRAMELIFT_PLUGIN_ABI_MAJOR, 2);
    EXPECT_EQ(FRAMELIFT_PLUGIN_ABI_MINOR, 2);
}

TEST(PluginAbiTest, OptionalMetadataDefaultsToNull)
{
    // Omitting publisher/description leaves them null (aggregate value-init).
    constexpr FrameLiftPluginInfo info{FRAMELIFT_PLUGIN_ABI_MAJOR, FRAMELIFT_PLUGIN_ABI_MINOR, FRAMELIFT_PLUGIN_ABI_PATCH, "Probe",
                                  {1, 0, 0}};
    EXPECT_EQ(info.publisher, nullptr);
    EXPECT_EQ(info.description, nullptr);
}

TEST(PluginAbiTest, OptionalMetadataWhenSet)
{
    constexpr FrameLiftPluginInfo info{FRAMELIFT_PLUGIN_ABI_MAJOR, FRAMELIFT_PLUGIN_ABI_MINOR, FRAMELIFT_PLUGIN_ABI_PATCH, "Probe",
                                  {1, 0, 0}, "Acme", "Does a thing"};
    EXPECT_STREQ(info.publisher, "Acme");
    EXPECT_STREQ(info.description, "Does a thing");
}
