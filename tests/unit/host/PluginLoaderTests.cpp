#include "PluginLoader.h"
#include "QtTestRunner.h"
#include <framelift/Log.h>
#include <set>
#include <string>

#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>
#include <QtTest/QTest>

Log::SinkFn HostLogSink()
{
    return nullptr;
}

class PluginLoaderTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void LoadAllRetainsSingleDiscoverySnapshot()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

#ifdef _WIN32
        const QString extension = ".dll";
#else
        const QString extension = ".so";
#endif
        const QString firstPath = dir.filePath("first" + extension);
        const QString secondPath = dir.filePath("second" + extension);
        for (const QString& path : {firstPath, secondPath})
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
        }

        PluginLoader loader;
        loader.LoadAll(dir.path().toStdString());
        QCOMPARE(loader.Plugins().size(), std::size_t{0});
        QCOMPARE(loader.AvailablePlugins().size(), std::size_t{2});

        QVERIFY(QFile::remove(firstPath));
        QVERIFY(QFile::remove(secondPath));

        std::set<std::string> snapshotIds;
        for (const auto& plugin : loader.AvailablePlugins())
        {
            snapshotIds.insert(plugin.pluginId);
        }
        QCOMPARE(
            snapshotIds, (std::set<std::string>{"first" + extension.toStdString(), "second" + extension.toStdString()})
        );
    }
};

namespace
{
const ::framelift::test::Registrar<PluginLoaderTest> kRegisterPluginLoaderTest{"PluginLoaderTest"};
}

#include "PluginLoaderTests.moc"
