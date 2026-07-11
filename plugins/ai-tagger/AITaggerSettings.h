#pragma once

#include <QtCore/QObject>
#include <QtCore/QVariantList>

class AITagger;

// AI Tagger's domain settings: per-folder rules and live tagging progress. The model
// list is a read-only projection used by the rule picker; installation, imports, tests,
// and cache policy live on Settings Menu's core AI page.
class AITaggerSettings final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString title READ Title CONSTANT)
    Q_PROPERTY(QVariantList models READ Models NOTIFY modelsChanged)
    Q_PROPERTY(QVariantList rules READ Rules NOTIFY rulesChanged)
    Q_PROPERTY(int maxInputSide READ MaxInputSide NOTIFY maxInputSideChanged)
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
    [[nodiscard]] int MaxInputSide() const;
    Q_INVOKABLE void setMaxInputSide(int value);

    [[nodiscard]] bool Tagging() const;
    [[nodiscard]] QString TaggingFile() const;
    [[nodiscard]] int TaggingDone() const;
    [[nodiscard]] int TaggingTotal() const;
    // Cancel the in-flight tagging run (delegates to the module).
    Q_INVOKABLE void cancelTagging();
    Q_INVOKABLE void load();

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
    void taggingChanged();
    void maxInputSideChanged();

private:
    AITagger& owner_;
};
