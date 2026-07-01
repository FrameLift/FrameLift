#include "PluginResolver.h"

#include <framelift/ModuleABI.h>

#include "QtTestRunner.h"

#include <QtTest/QtTest>
#include <string>
#include <utility>
#include <vector>

namespace
{
std::vector<std::string> EmptyList()
{
    return {};
}

std::vector<std::string> List(std::initializer_list<const char*> items)
{
    return {items.begin(), items.end()};
}

struct PluginFixture
{
    PluginMetadata info{};

    PluginFixture(
        std::string pluginId, std::vector<std::string> provides, std::vector<std::string> reqPlugins,
        std::vector<std::string> reqFeatures, std::vector<std::string> optional, std::vector<std::string> platformList
    )
    {
        info.valid = true;
        info.abiVersion = FRAMELIFT_ABI_VERSION;
        info.pluginId = std::move(pluginId);
        info.name = info.pluginId;
        info.providesFeatures = std::move(provides);
        info.requiresPlugins = std::move(reqPlugins);
        info.requiresFeatures = std::move(reqFeatures);
        info.optionalFeatures = std::move(optional);
        info.platforms = std::move(platformList);
    }
};
} // namespace

class PluginResolverTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void AcceptsValidDependencyGraph()
    {
        PluginFixture provider{"framelift.history", List({"history.service"}), EmptyList(), EmptyList(), EmptyList(),
                               EmptyList()};
        PluginFixture consumer{"framelift.playlist",      EmptyList(), EmptyList(),
                               List({"history.service"}), EmptyList(), EmptyList()};

        const auto decisions = ResolvePlugins({{&provider.info}, {&consumer.info}}, "linux");

        QVERIFY((decisions.size()) == (2u));
        QVERIFY(decisions[0].accepted);
        QVERIFY(decisions[1].accepted);
    }

    void RejectsMissingRequiredPlugin()
    {
        PluginFixture consumer{"framelift.playlist", EmptyList(), List({"framelift.history"}),
                               EmptyList(),          EmptyList(), EmptyList()};

        const auto decisions = ResolvePlugins({{&consumer.info}}, "linux");

        QVERIFY((decisions.size()) == (1u));
        QVERIFY(!(decisions[0].accepted));
        QVERIFY((decisions[0].reason.find("framelift.history")) != (std::string::npos));
    }

    void RejectsMissingRequiredFeature()
    {
        PluginFixture consumer{"framelift.playlist",      EmptyList(), EmptyList(),
                               List({"history.service"}), EmptyList(), EmptyList()};

        const auto decisions = ResolvePlugins({{&consumer.info}}, "linux");

        QVERIFY((decisions.size()) == (1u));
        QVERIFY(!(decisions[0].accepted));
        QVERIFY((decisions[0].reason.find("history.service")) != (std::string::npos));
    }

    void CascadesRejectedDependencies()
    {
        PluginFixture provider{"framelift.provider", EmptyList(), EmptyList(),
                               EmptyList(),          EmptyList(), List({"windows"})};
        PluginFixture consumer{"framelift.consumer", EmptyList(), List({"framelift.provider"}),
                               EmptyList(),          EmptyList(), EmptyList()};

        const auto decisions = ResolvePlugins({{&provider.info}, {&consumer.info}}, "linux");

        QVERIFY((decisions.size()) == (2u));
        QVERIFY(!(decisions[0].accepted));
        QVERIFY(!(decisions[1].accepted));
        QVERIFY((decisions[1].reason.find("framelift.provider")) != (std::string::npos));
    }

    void OptionalFeaturesDoNotGateLoading()
    {
        PluginFixture package{"framelift.optional",       EmptyList(), EmptyList(), EmptyList(),
                              List({"missing.optional"}), EmptyList()};

        const auto decisions = ResolvePlugins({{&package.info}}, "linux");

        QVERIFY((decisions.size()) == (1u));
        QVERIFY(decisions[0].accepted);
    }

    void OrdersProviderBeforeOptionalConsumer()
    {
        PluginFixture provider{
            "framelift.context_menu", List({"ui.context_menu"}), EmptyList(), EmptyList(), EmptyList(), EmptyList()
        };
        PluginFixture consumer{"framelift.playlist",      EmptyList(), EmptyList(), EmptyList(),
                               List({"ui.context_menu"}), EmptyList()};

        // Consumer listed first in the input; ordering must still load the provider first.
        const auto order = OrderPlugins({{&consumer.info}, {&provider.info}});
        QVERIFY((order.size()) == (2u));
        QVERIFY((order[0]) == (1u)); // provider (input index 1)
        QVERIFY((order[1]) == (0u)); // consumer (input index 0)
    }

    void OrdersIndependentPackagesByPackageId()
    {
        PluginFixture zzz{"framelift.zzz", EmptyList(), EmptyList(), EmptyList(), EmptyList(), EmptyList()};
        PluginFixture aaa{"framelift.aaa", EmptyList(), EmptyList(), EmptyList(), EmptyList(), EmptyList()};

        const auto order = OrderPlugins({{&zzz.info}, {&aaa.info}});
        QVERIFY((order.size()) == (2u));
        QVERIFY((order[0]) == (1u)); // framelift.aaa sorts first
        QVERIFY((order[1]) == (0u));
    }
};

namespace
{
const ::framelift::test::Registrar<PluginResolverTest> kRegisterPluginResolverTest{"PluginResolverTest"};
}

#include "PluginResolverTests.moc"
