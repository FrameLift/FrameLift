#include "AITaggerSettings.h"

#include "AITagger.h"
#include "TagStore.h"

#include <QtCore/QStringList>
#include <QtCore/QVariantMap>

#include <algorithm>

AITaggerSettings::AITaggerSettings(AITagger& owner) : owner_(owner)
{
    // Mirror the module's live tagging progress into this page's `tagging*` properties.
    connect(&owner_, &AITagger::progressChanged, this, &AITaggerSettings::taggingChanged);
}

AITaggerSettings::~AITaggerSettings() = default;

QString AITaggerSettings::Title() const
{
    return QStringLiteral("AI Tagger");
}

QVariantList AITaggerSettings::Models() const
{
    QVariantList out;
    if (auto* models = owner_.ModelsForSettings())
    {
        models->Enumerate(
            [](const AIModelInfo* info, void* ud)
            {
                if (!info)
                {
                    return;
                }
                auto* rows = static_cast<QVariantList*>(ud);
                QVariantMap row;
                row["id"] = QString::fromUtf8(info->id ? info->id : "");
                row["name"] = QString::fromUtf8(info->name ? info->name : "");
                row["installed"] = info->installed;
                rows->push_back(row);
            },
            &out
        );
    }
    return out;
}

QVariantList AITaggerSettings::Rules() const
{
    QVariantList out;
    TagStore* store = owner_.StoreForSettings();
    if (!store)
    {
        return out;
    }
    for (const aitagger::TagRule& r : store->ListRules())
    {
        QStringList lines;
        QVariantList entries;
        for (const aitagger::RuleEntry& e : r.entries)
        {
            lines << (QString::fromStdString(e.question) + " => " + QString::fromStdString(e.tag));
            QVariantMap em;
            em["question"] = QString::fromStdString(e.question);
            em["tag"] = QString::fromStdString(e.tag);
            em["threshold"] = e.threshold; // negative ⇒ use rule default
            em["analysisMode"] = static_cast<int>(e.analysisMode);
            entries.push_back(em);
        }
        QVariantMap m;
        m["id"] = static_cast<qint64>(r.id);
        m["folder"] = QString::fromStdString(r.folder);
        m["modelId"] = QString::fromStdString(r.modelId);
        m["questions"] = lines.join('\n'); // one-line summary for the rule list
        m["entries"] = entries;
        m["threshold"] = r.threshold;
        m["frameBudget"] = r.frameBudget;
        m["watch"] = r.watch;
        out.push_back(m);
    }
    return out;
}

int AITaggerSettings::MaxInputSide() const
{
    return owner_.MaxInputSide();
}

void AITaggerSettings::setMaxInputSide(int value)
{
    const int before = owner_.MaxInputSide();
    owner_.SetMaxInputSide(value);
    if (owner_.MaxInputSide() != before)
    {
        Q_EMIT maxInputSideChanged();
    }
}

bool AITaggerSettings::Tagging() const
{
    return owner_.IsRunning();
}

QString AITaggerSettings::TaggingFile() const
{
    return owner_.CurrentFile();
}

int AITaggerSettings::TaggingDone() const
{
    return owner_.FilesDone();
}

int AITaggerSettings::TaggingTotal() const
{
    return owner_.FilesTotal();
}

void AITaggerSettings::cancelTagging()
{
    owner_.cancel();
}

void AITaggerSettings::load()
{
    Q_EMIT modelsChanged();
    Q_EMIT rulesChanged();
    Q_EMIT taggingChanged();
}

void AITaggerSettings::saveRule(
    const QString& folder, const QString& modelId, const QVariantList& questions, double threshold, int frameBudget,
    bool watch
)
{
    TagStore* store = owner_.StoreForSettings();
    if (!store || folder.isEmpty())
    {
        return;
    }
    aitagger::TagRule rule;
    rule.folder = folder.toStdString();
    rule.modelId = modelId.toStdString();
    rule.threshold = static_cast<float>(threshold);
    rule.frameBudget = frameBudget > 0 ? frameBudget : 31;
    rule.watch = watch;
    for (const QVariant& row : questions)
    {
        const QVariantMap m = row.toMap();
        aitagger::RuleEntry e;
        e.question = m.value("question").toString().trimmed().toStdString();
        e.tag = m.value("tag").toString().trimmed().toStdString();
        if (e.question.empty() || e.tag.empty())
        {
            continue; // require both a question and the tag it produces
        }
        // Empty/absent threshold ⇒ -1 ⇒ use the rule default at tagging time.
        const QVariant t = m.value("threshold");
        bool ok = false;
        const double parsed = t.toDouble(&ok);
        e.threshold = (ok && parsed >= 0.0) ? static_cast<float>(parsed) : -1.0f;
        e.analysisMode = static_cast<aitagger::AnalysisMode>(std::clamp(m.value("analysisMode").toInt(), 0, 2));
        rule.entries.push_back(std::move(e));
    }
    store->UpsertRule(rule);
    owner_.OnRulesChanged();
    Q_EMIT rulesChanged();
}

void AITaggerSettings::deleteRule(qint64 ruleId)
{
    TagStore* store = owner_.StoreForSettings();
    if (!store)
    {
        return;
    }
    store->DeleteRule(ruleId);
    owner_.OnRulesChanged();
    Q_EMIT rulesChanged();
}

void AITaggerSettings::tagFolder(const QString& folder)
{
    owner_.tagFolder(folder);
}
