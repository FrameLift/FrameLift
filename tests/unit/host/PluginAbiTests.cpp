#include "PluginMetadata.h"

#include <framelift/ModuleABI.h>

#include <QJsonArray>
#include <QJsonObject>

#include "QtTestRunner.h"

#include <QtTest/QtTest>

class PluginAbiTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void MatchingEpochLoads()
    {
        QVERIFY(FrameLiftAbiCompatible(FRAMELIFT_ABI_VERSION, FRAMELIFT_ABI_VERSION));
    }

    void MismatchedEpochRejectedEitherDirection()
    {
        QVERIFY(!(FrameLiftAbiCompatible(FRAMELIFT_ABI_VERSION - 1, FRAMELIFT_ABI_VERSION)));
        QVERIFY(!(FrameLiftAbiCompatible(FRAMELIFT_ABI_VERSION + 1, FRAMELIFT_ABI_VERSION)));
    }

    void MetadataParsesCurrentPluginShape()
    {
        const PluginMetadata info = ParsePluginMetadata(
            QJsonObject{
                {"abi", FRAMELIFT_ABI_VERSION},
                {"pluginId", "framelift.playlist"},
                {"pluginFile", "framelift.playlist"},
                {"name", "Playlist"},
                {"version", QJsonArray{1, 2, 3}},
                {"publisher", "FrameLift"},
                {"description", "Folder playlist"},
                {"providesFeatures", QJsonArray{"playlist.panel", "playlist.navigation"}},
                {"requiresPlugins", QJsonArray{"framelift.history"}},
                {"requiresFeatures", QJsonArray{"history.service"}},
                {"optionalPlugins", QJsonArray{"framelift.context-menu"}},
                {"optionalFeatures", QJsonArray{"ui.context_menu"}},
                {"platforms", QJsonArray{"linux", "windows"}},
            }
        );

        QVERIFY(info.valid);
        QVERIFY((info.abiVersion) == (FRAMELIFT_ABI_VERSION));
        QVERIFY((info.pluginId) == ("framelift.playlist"));
        QVERIFY((info.pluginFile) == ("framelift.playlist"));
        QVERIFY((info.name) == ("Playlist"));
        QVERIFY((info.version[0]) == (1));
        QVERIFY((info.version[1]) == (2));
        QVERIFY((info.version[2]) == (3));
        QVERIFY((info.publisher) == ("FrameLift"));
        QVERIFY((info.description) == ("Folder playlist"));
        QVERIFY((info.providesFeatures.size()) == (2u));
        QVERIFY((info.providesFeatures[1]) == ("playlist.navigation"));
        QVERIFY((info.requiresPlugins.size()) == (1u));
        QVERIFY((info.requiresPlugins[0]) == ("framelift.history"));
        QVERIFY((info.requiresFeatures.size()) == (1u));
        QVERIFY((info.requiresFeatures[0]) == ("history.service"));
        QVERIFY((info.optionalPlugins.size()) == (1u));
        QVERIFY((info.optionalPlugins[0]) == ("framelift.context-menu"));
        QVERIFY((info.optionalFeatures.size()) == (1u));
        QVERIFY((info.optionalFeatures[0]) == ("ui.context_menu"));
        QVERIFY((info.platforms.size()) == (2u));
        QVERIFY((info.platforms[0]) == ("linux"));
    }

    void MetadataWithoutPluginIdIsInvalid()
    {
        const PluginMetadata info = ParsePluginMetadata(QJsonObject{{"abi", FRAMELIFT_ABI_VERSION}});
        QVERIFY(!(info.valid));
    }
};

namespace
{
const ::framelift::test::Registrar<PluginAbiTest> kRegisterPluginAbiTest{"PluginAbiTest"};
}

#include "PluginAbiTests.moc"
