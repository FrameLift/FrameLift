#include "PackageResolver.h"

#include <gtest/gtest.h>
#include <string>

namespace
{
constexpr FrameLiftStringList EmptyList()
{
    return {nullptr, 0};
}

template <int N>
constexpr FrameLiftStringList List(const char* const (&items)[N])
{
    return {items, N};
}

struct PackageFixture
{
    FrameLiftModuleInfo module{};
    FrameLiftPackageInfo info{};

    PackageFixture(
        const char* packageId, const char* moduleId, FrameLiftStringList provides, FrameLiftStringList reqModules,
        FrameLiftStringList reqFeatures, FrameLiftStringList optional, FrameLiftStringList platformList
    )
        : module{moduleId, "Module", nullptr, provides, reqModules, reqFeatures, EmptyList(), optional, platformList},
          info{FRAMELIFT_ABI_VERSION,
               packageId,
               packageId,
               packageId,
               {1, 0, 0},
               nullptr,
               nullptr,
               &module,
               1}
    {
    }
};
} // namespace

TEST(PackageResolverTest, AcceptsValidDependencyGraph)
{
    static constexpr const char* const providerFeatures[] = {"history.service"};
    static constexpr const char* const consumerRequires[] = {"history.service"};
    PackageFixture provider{
        "framelift.history", "framelift.history.core", List(providerFeatures), EmptyList(), EmptyList(), EmptyList(),
        EmptyList()};
    PackageFixture consumer{
        "framelift.playlist", "framelift.playlist.core", EmptyList(), EmptyList(), List(consumerRequires), EmptyList(),
        EmptyList()};

    const auto decisions = ResolvePackages({{&provider.info}, {&consumer.info}}, "linux");

    ASSERT_EQ(decisions.size(), 2u);
    EXPECT_TRUE(decisions[0].accepted);
    EXPECT_TRUE(decisions[1].accepted);
}

TEST(PackageResolverTest, RejectsMissingRequiredModule)
{
    static constexpr const char* const requiredModules[] = {"framelift.history.core"};
    PackageFixture consumer{
        "framelift.playlist", "framelift.playlist.core", EmptyList(), List(requiredModules), EmptyList(), EmptyList(),
        EmptyList()};

    const auto decisions = ResolvePackages({{&consumer.info}}, "linux");

    ASSERT_EQ(decisions.size(), 1u);
    EXPECT_FALSE(decisions[0].accepted);
    EXPECT_NE(decisions[0].reason.find("framelift.history.core"), std::string::npos);
}

TEST(PackageResolverTest, RejectsMissingRequiredFeature)
{
    static constexpr const char* const requiredFeatures[] = {"history.service"};
    PackageFixture consumer{
        "framelift.playlist", "framelift.playlist.core", EmptyList(), EmptyList(), List(requiredFeatures), EmptyList(),
        EmptyList()};

    const auto decisions = ResolvePackages({{&consumer.info}}, "linux");

    ASSERT_EQ(decisions.size(), 1u);
    EXPECT_FALSE(decisions[0].accepted);
    EXPECT_NE(decisions[0].reason.find("history.service"), std::string::npos);
}

TEST(PackageResolverTest, RejectsUnsupportedPlatform)
{
    static constexpr const char* const platforms[] = {"windows"};
    PackageFixture updater{
        "framelift.updater", "framelift.updater.core", EmptyList(), EmptyList(), EmptyList(), EmptyList(),
        List(platforms)};

    const auto decisions = ResolvePackages({{&updater.info}}, "linux");

    ASSERT_EQ(decisions.size(), 1u);
    EXPECT_FALSE(decisions[0].accepted);
    EXPECT_NE(decisions[0].reason.find("platform"), std::string::npos);
}

TEST(PackageResolverTest, CascadesRejectedDependencies)
{
    static constexpr const char* const windowsOnly[] = {"windows"};
    static constexpr const char* const requiresModule[] = {"framelift.provider.core"};
    PackageFixture provider{
        "framelift.provider", "framelift.provider.core", EmptyList(), EmptyList(), EmptyList(), EmptyList(),
        List(windowsOnly)};
    PackageFixture consumer{
        "framelift.consumer", "framelift.consumer.core", EmptyList(), List(requiresModule), EmptyList(), EmptyList(),
        EmptyList()};

    const auto decisions = ResolvePackages({{&provider.info}, {&consumer.info}}, "linux");

    ASSERT_EQ(decisions.size(), 2u);
    EXPECT_FALSE(decisions[0].accepted);
    EXPECT_FALSE(decisions[1].accepted);
    EXPECT_NE(decisions[1].reason.find("framelift.provider.core"), std::string::npos);
}

TEST(PackageResolverTest, OptionalFeaturesDoNotGateLoading)
{
    static constexpr const char* const optional[] = {"missing.optional"};
    PackageFixture package{
        "framelift.optional", "framelift.optional.core", EmptyList(), EmptyList(), EmptyList(), List(optional),
        EmptyList()};

    const auto decisions = ResolvePackages({{&package.info}}, "linux");

    ASSERT_EQ(decisions.size(), 1u);
    EXPECT_TRUE(decisions[0].accepted);
}

TEST(PackageResolverTest, OrdersProviderBeforeOptionalConsumer)
{
    static constexpr const char* const provides[] = {"ui.context_menu"};
    static constexpr const char* const optional[] = {"ui.context_menu"};
    PackageFixture provider{
        "framelift.context_menu", "framelift.context_menu.core", List(provides), EmptyList(), EmptyList(), EmptyList(),
        EmptyList()};
    PackageFixture consumer{
        "framelift.playlist", "framelift.playlist.core", EmptyList(), EmptyList(), EmptyList(), List(optional),
        EmptyList()};

    // Consumer listed first in the input; ordering must still load the provider first.
    const auto order = OrderPackages({{&consumer.info}, {&provider.info}});
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], 1u); // provider (input index 1)
    EXPECT_EQ(order[1], 0u); // consumer (input index 0)
}

TEST(PackageResolverTest, OrdersIndependentPackagesByPackageId)
{
    PackageFixture zzz{
        "framelift.zzz", "framelift.zzz.core", EmptyList(), EmptyList(), EmptyList(), EmptyList(), EmptyList()};
    PackageFixture aaa{
        "framelift.aaa", "framelift.aaa.core", EmptyList(), EmptyList(), EmptyList(), EmptyList(), EmptyList()};

    const auto order = OrderPackages({{&zzz.info}, {&aaa.info}});
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], 1u); // framelift.aaa sorts first
    EXPECT_EQ(order[1], 0u);
}
