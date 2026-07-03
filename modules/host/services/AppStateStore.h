#pragma once

#include <QtCore/QString>

// Small INI-backed runtime state store. This is separate from user settings:
// values here are remembered app state such as "where did the last dialog open?".
class AppStateStore
{
public:
    AppStateStore();
    explicit AppStateStore(QString stateDir);

    [[nodiscard]] QString FileDialogLastDirectory() const;
    void SaveFileDialogLastDirectory(const QString& directory) const;
    void SaveFileDialogLastDirectoryForFile(const QString& filePath) const;

    [[nodiscard]] QString StateFilePath() const;

private:
    QString stateDir_;
};
