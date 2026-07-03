#include "AppStateStore.h"

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QSettings>
#include <QtCore/QStandardPaths>

#include <utility>

namespace
{
constexpr auto kStateFileName = "state.ini";
constexpr auto kFileDialogLastDirectoryKey = "fileDialog/lastDirectory";
} // namespace

AppStateStore::AppStateStore() : AppStateStore(QStandardPaths::writableLocation(QStandardPaths::StateLocation))
{
}

AppStateStore::AppStateStore(QString stateDir) : stateDir_(std::move(stateDir))
{
}

QString AppStateStore::StateFilePath() const
{
    return stateDir_.isEmpty() ? QString() : QDir(stateDir_).filePath(QLatin1String(kStateFileName));
}

QString AppStateStore::FileDialogLastDirectory() const
{
    const QString stateFile = StateFilePath();
    if (stateFile.isEmpty())
    {
        return {};
    }

    const QSettings state(stateFile, QSettings::IniFormat);
    const QString directory = state.value(QLatin1String(kFileDialogLastDirectoryKey)).toString();
    return QFileInfo(directory).isDir() ? directory : QString();
}

void AppStateStore::SaveFileDialogLastDirectory(const QString& directory) const
{
    const QString stateFile = StateFilePath();
    if (stateFile.isEmpty() || directory.isEmpty())
    {
        return;
    }

    QDir().mkpath(stateDir_);
    QSettings state(stateFile, QSettings::IniFormat);
    state.setValue(QLatin1String(kFileDialogLastDirectoryKey), directory);
    state.sync();
}

void AppStateStore::SaveFileDialogLastDirectoryForFile(const QString& filePath) const
{
    const QFileInfo file(filePath);
    SaveFileDialogLastDirectory(file.absoluteDir().absolutePath());
}
