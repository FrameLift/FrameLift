#pragma once

#include "ModelCatalog.h"

#include <QtCore/QObject>
#include <QtCore/QString>
#include <memory>

QT_BEGIN_NAMESPACE
class QNetworkAccessManager;
class QNetworkReply;
class QFile;
QT_END_NAMESPACE

// Downloads a catalogue entry's two GGUF files into a target directory, resuming a
// partial ".part" file with an HTTP Range request and verifying sha256 on completion.
// One download at a time; progress and completion are reported via signals so the
// settings view model can drive the QML. The model directory is <exeDir>/models/.
class ModelDownloader final : public QObject
{
    Q_OBJECT

public:
    explicit ModelDownloader(QObject* parent = nullptr);
    ~ModelDownloader() override;

    // Absolute path to the models directory (created on demand).
    [[nodiscard]] static QString ModelsDir();
    // On-disk paths for an installed model id.
    [[nodiscard]] static QString ModelPath(const QString& id);
    [[nodiscard]] static QString MmprojPath(const QString& id);
    // Both files present (a completed install).
    [[nodiscard]] static bool IsInstalled(const QString& id);

    [[nodiscard]] bool IsBusy() const
    {
        return busy_;
    }

    [[nodiscard]] QString CurrentId() const
    {
        return entry_.id.empty() ? QString() : QString::fromStdString(entry_.id);
    }

    // Start downloading `entry` (model then mmproj). No-op if already busy.
    void Start(const aitagger::CatalogEntry& entry);
    // Cancel the in-flight download (keeps the .part for a later resume).
    void Cancel();

Q_SIGNALS:
    void progress(QString id, double fraction); // 0..1 across both files
    void finished(QString id, bool ok, QString error);

private:
    void StartFile(int which); // 0 = model, 1 = mmproj
    void OnReadyRead();
    void OnFinished();
    void Fail(const QString& error);
    void Cleanup();
    [[nodiscard]] bool VerifySha(const QString& path, const std::string& expectedHex) const;

    std::unique_ptr<QNetworkAccessManager> nam_;
    aitagger::CatalogEntry entry_;
    QNetworkReply* reply_ = nullptr;
    std::unique_ptr<QFile> file_;
    QString targetPath_; // final path for the current file
    int which_ = 0;      // 0 model, 1 mmproj
    qint64 resumeFrom_ = 0;
    qint64 received_ = 0;
    qint64 total_ = 0;
    bool busy_ = false;
};
