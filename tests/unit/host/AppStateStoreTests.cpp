#include "AppStateStore.h"
#include "TempIni.h"

#include "QtTestRunner.h"

#include <QtCore/QDir>
#include <QtCore/QSettings>
#include <QtTest/QtTest>

#include <filesystem>
#include <fstream>

namespace
{
QString ToQString(const std::filesystem::path& path)
{
    return QString::fromStdString(path.string());
}

class TempDir
{
public:
    TempDir()
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        std::filesystem::create_directories(path, ec);
    }

    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }

    [[nodiscard]] QString qstr() const
    {
        return ToQString(path);
    }

    std::filesystem::path path = UniqueTempPath();
};
} // namespace

class AppStateStoreTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void MissingStateReturnsNoDirectory()
    {
        const TempDir stateDir;
        const AppStateStore store(stateDir.qstr());

        QVERIFY(store.FileDialogLastDirectory().isEmpty());
    }

    void SaveFilePersistsParentDirectory()
    {
        const TempDir stateDir;
        const TempDir mediaDir;
        const std::filesystem::path mediaFile = mediaDir.path / "movie.mkv";
        std::ofstream(mediaFile) << "x";

        const AppStateStore store(stateDir.qstr());
        store.SaveFileDialogLastDirectoryForFile(ToQString(mediaFile));

        QCOMPARE(QDir(store.FileDialogLastDirectory()).absolutePath(), QDir(mediaDir.qstr()).absolutePath());
    }

    void InvalidCachedDirectoryIsIgnored()
    {
        const TempDir stateDir;
        const AppStateStore store(stateDir.qstr());

        QSettings state(store.StateFilePath(), QSettings::IniFormat);
        state.setValue("fileDialog/lastDirectory", ToQString(stateDir.path / "missing"));
        state.sync();

        QVERIFY(store.FileDialogLastDirectory().isEmpty());
    }

    void SaveCreatesStateDirectory()
    {
        const std::filesystem::path stateDir = UniqueTempPath();
        std::error_code ec;
        std::filesystem::remove_all(stateDir, ec);

        const TempDir mediaDir;
        const AppStateStore store(ToQString(stateDir));
        store.SaveFileDialogLastDirectory(mediaDir.qstr());

        QVERIFY(std::filesystem::is_directory(stateDir));
        QVERIFY(std::filesystem::is_regular_file(stateDir / "state.ini"));

        std::filesystem::remove_all(stateDir, ec);
    }

    void SavePreservesUnrelatedStateKeys()
    {
        const TempDir stateDir;
        const TempDir mediaDir;
        const AppStateStore store(stateDir.qstr());

        QSettings state(store.StateFilePath(), QSettings::IniFormat);
        state.setValue("app/startCount", 7);
        state.sync();

        store.SaveFileDialogLastDirectory(mediaDir.qstr());

        QSettings reloaded(store.StateFilePath(), QSettings::IniFormat);
        QCOMPARE(reloaded.value("app/startCount").toInt(), 7);
        QCOMPARE(
            QDir(reloaded.value("fileDialog/lastDirectory").toString()).absolutePath(),
            QDir(mediaDir.qstr()).absolutePath()
        );
    }
};

namespace
{
const ::framelift::test::Registrar<AppStateStoreTest> kRegisterAppStateStoreTest{"AppStateStoreTest"};
}

#include "AppStateStoreTests.moc"
