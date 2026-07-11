#pragma once

#include "ModelCatalog.h"

#include <QtCore/QObject>
#include <QtCore/QVariantList>
#include <memory>

class AITagger;
class ModelDownloader;

// View model for the AI Tagger settings page (rendered by SettingsMenu). Surfaces the
// model catalogue with install/download state and the per-folder tagging rules, and
// exposes the actions QML calls: download/cancel a model, add/update/delete a rule.
// Rule questions are edited as one "question => tag" per line for a simple UI.
class AITaggerSettings final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString title READ Title CONSTANT)
    Q_PROPERTY(QVariantList models READ Models NOTIFY modelsChanged)
    Q_PROPERTY(QVariantList rules READ Rules NOTIFY rulesChanged)
    Q_PROPERTY(QString downloadingId READ DownloadingId NOTIFY downloadChanged)
    Q_PROPERTY(double downloadProgress READ DownloadProgress NOTIFY downloadChanged)
    Q_PROPERTY(QString lastError READ LastError NOTIFY downloadChanged)
    Q_PROPERTY(QString testingId READ TestingId NOTIFY testChanged)
    Q_PROPERTY(QString testStatus READ TestStatus NOTIFY testChanged)
    Q_PROPERTY(double testMsPerFrame READ TestMsPerFrame NOTIFY testChanged)
    // Live folder-tagging progress, proxied from the owning AITagger module so the
    // settings page can show a status strip (the video overlay pill shows the same state).
    Q_PROPERTY(bool tagging READ Tagging NOTIFY taggingChanged)
    Q_PROPERTY(QString taggingFile READ TaggingFile NOTIFY taggingChanged)
    Q_PROPERTY(int taggingDone READ TaggingDone NOTIFY taggingChanged)
    Q_PROPERTY(int taggingTotal READ TaggingTotal NOTIFY taggingChanged)

public:
    explicit AITaggerSettings(AITagger& owner);
    ~AITaggerSettings() override;

    [[nodiscard]] QString Title() const;
    [[nodiscard]] QVariantList Models() const;
    [[nodiscard]] QVariantList Rules() const;
    [[nodiscard]] QString DownloadingId() const;
    [[nodiscard]] double DownloadProgress() const;
    [[nodiscard]] QString LastError() const;
    [[nodiscard]] QString TestingId() const;
    [[nodiscard]] QString TestStatus() const;
    [[nodiscard]] double TestMsPerFrame() const;

    [[nodiscard]] bool Tagging() const;
    [[nodiscard]] QString TaggingFile() const;
    [[nodiscard]] int TaggingDone() const;
    [[nodiscard]] int TaggingTotal() const;
    // Cancel the in-flight tagging run (delegates to the module).
    Q_INVOKABLE void cancelTagging();

    Q_INVOKABLE void download(const QString& id);
    Q_INVOKABLE void cancelDownload();

    // Load `id` and run inference on a synthetic frame on a background thread, reporting
    // whether it works and the average milliseconds to sample+infer one frame — a
    // benchmark so users can pick a model their hardware can keep up with.
    Q_INVOKABLE void testModel(const QString& id);

    // folder + model + a list of { question, tag, threshold } entry maps + params.
    // A negative/empty per-entry threshold means "use the rule default".
    Q_INVOKABLE void saveRule(
        const QString& folder, const QString& modelId, const QVariantList& questions, double threshold, int frameBudget,
        bool watch
    );
    Q_INVOKABLE void deleteRule(qint64 ruleId);
    // Kick a tagging run over a rule's folder now.
    Q_INVOKABLE void tagFolder(const QString& folder);

Q_SIGNALS:
    void modelsChanged();
    void rulesChanged();
    void downloadChanged();
    void testChanged();
    void taggingChanged();

private:
    AITagger& owner_;
    std::unique_ptr<ModelDownloader> downloader_;
    QString downloadingId_;
    double downloadProgress_ = 0.0;
    QString lastError_;

    QString testingId_;
    QString testStatus_;
    double testMs_ = 0.0;
};
