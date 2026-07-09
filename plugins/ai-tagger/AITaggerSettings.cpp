#include "AITaggerSettings.h"

#include "AITagger.h"
#include "InferenceBackend.h"
#include "ModelDownloader.h"
#include "TagStore.h"

#include <QtCore/QPointer>
#include <QtCore/QStringList>
#include <QtCore/QVariantMap>

#include <chrono>
#include <thread>
#include <vector>

AITaggerSettings::AITaggerSettings(AITagger& owner) : owner_(owner), downloader_(std::make_unique<ModelDownloader>())
{
    connect(
        downloader_.get(), &ModelDownloader::progress, this,
        [this](const QString& id, double frac)
        {
            downloadingId_ = id;
            downloadProgress_ = frac;
            Q_EMIT downloadChanged();
        }
    );
    connect(
        downloader_.get(), &ModelDownloader::finished, this,
        [this](const QString& id, bool ok, const QString& err)
        {
            downloadingId_.clear();
            downloadProgress_ = 0.0;
            lastError_ = ok ? QString() : (id + ": " + err);
            Q_EMIT downloadChanged();
            Q_EMIT modelsChanged();
        }
    );
}

AITaggerSettings::~AITaggerSettings() = default;

QString AITaggerSettings::Title() const
{
    return QStringLiteral("AI Tagger");
}

QVariantList AITaggerSettings::Models() const
{
    QVariantList out;
    for (const aitagger::CatalogEntry& e : aitagger::BuiltinCatalog())
    {
        const QString id = QString::fromStdString(e.id);
        QVariantMap m;
        m["id"] = id;
        m["name"] = QString::fromStdString(e.name);
        m["quant"] = QString::fromStdString(e.quant);
        m["recommended"] = e.recommended;
        m["installed"] = ModelDownloader::IsInstalled(id);
        m["downloading"] = (id == downloadingId_);
        m["progress"] = (id == downloadingId_) ? downloadProgress_ : 0.0;
        out.push_back(m);
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
        for (const aitagger::RuleEntry& e : r.entries)
        {
            lines << (QString::fromStdString(e.question) + " => " + QString::fromStdString(e.tag));
        }
        QVariantMap m;
        m["id"] = static_cast<qint64>(r.id);
        m["folder"] = QString::fromStdString(r.folder);
        m["modelId"] = QString::fromStdString(r.modelId);
        m["questions"] = lines.join('\n');
        m["threshold"] = r.threshold;
        m["frameBudget"] = r.frameBudget;
        m["watch"] = r.watch;
        out.push_back(m);
    }
    return out;
}

QString AITaggerSettings::DownloadingId() const
{
    return downloadingId_;
}

double AITaggerSettings::DownloadProgress() const
{
    return downloadProgress_;
}

QString AITaggerSettings::LastError() const
{
    return lastError_;
}

QString AITaggerSettings::TestingId() const
{
    return testingId_;
}

QString AITaggerSettings::TestStatus() const
{
    return testStatus_;
}

double AITaggerSettings::TestMsPerFrame() const
{
    return testMs_;
}

void AITaggerSettings::testModel(const QString& id)
{
    if (!testingId_.isEmpty())
    {
        return; // a test is already running
    }
    if (!ModelDownloader::IsInstalled(id))
    {
        testingId_ = id;
        testStatus_ = "Model not installed";
        testMs_ = 0.0;
        Q_EMIT testChanged();
        testingId_.clear();
        Q_EMIT testChanged();
        return;
    }
    testingId_ = id;
    testStatus_ = "Loading…";
    testMs_ = 0.0;
    Q_EMIT testChanged();

    const std::string modelPath = ModelDownloader::ModelPath(id).toStdString();
    const std::string mmprojPath = ModelDownloader::MmprojPath(id).toStdString();
    const std::string modelId = id.toStdString();

    // Run the (slow) load + inference off the UI thread; marshal the result back with a
    // QPointer guard so a destroyed page just drops the callback.
    QPointer<AITaggerSettings> self(this);
    std::thread(
        [self, modelPath, mmprojPath, modelId]
        {
            auto report = [self](const QString& status, double ms)
            {
                QMetaObject::invokeMethod(
                    self ? self.data() : nullptr,
                    [self, status, ms]
                    {
                        if (!self)
                        {
                            return;
                        }
                        self->testStatus_ = status;
                        self->testMs_ = ms;
                        Q_EMIT self->testChanged();
                        self->testingId_.clear();
                        Q_EMIT self->testChanged();
                    },
                    Qt::QueuedConnection
                );
            };

            auto backend = aitagger::CreateLlamaBackend();
            aitagger::ModelSpec spec;
            spec.modelPath = modelPath;
            spec.mmprojPath = mmprojPath;
            spec.modelId = modelId;
            std::string err;
            if (!backend->LoadModel(spec, err))
            {
                report(QString::fromStdString("Load failed: " + err), 0.0);
                return;
            }

            // A synthetic mostly-red 448x448 frame; a sanity question we can predict.
            constexpr int kW = 448;
            constexpr int kH = 448;
            std::vector<std::uint8_t> rgba(static_cast<std::size_t>(kW) * kH * 4);
            for (std::size_t i = 0; i < static_cast<std::size_t>(kW) * kH; ++i)
            {
                rgba[i * 4 + 0] = 210; // R
                rgba[i * 4 + 1] = 30;  // G
                rgba[i * 4 + 2] = 30;  // B
                rgba[i * 4 + 3] = 255;
            }
            const std::vector<aitagger::BackendQuestion> qs = {{"Is the image mostly red?", "red"}};
            std::vector<float> out;

            // One warmup, then time three frames.
            if (!backend->EvaluateFrame(rgba.data(), kW, kH, qs, out, err))
            {
                report(QString::fromStdString("Inference failed: " + err), 0.0);
                return;
            }
            const auto t0 = std::chrono::steady_clock::now();
            constexpr int kRuns = 3;
            for (int r = 0; r < kRuns; ++r)
            {
                if (!backend->EvaluateFrame(rgba.data(), kW, kH, qs, out, err))
                {
                    report(QString::fromStdString("Inference failed: " + err), 0.0);
                    return;
                }
            }
            const auto t1 = std::chrono::steady_clock::now();
            const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count() / kRuns;
            const double pRed = out.empty() ? 0.0 : out[0];
            backend->UnloadModel();

            const QString status = pRed >= 0.5 ? QString("Working — P(red)=%1").arg(pRed, 0, 'f', 2)
                                               : QString("Loaded but weak result — P(red)=%1").arg(pRed, 0, 'f', 2);
            report(status, ms);
        }
    ).detach();
}

void AITaggerSettings::download(const QString& id)
{
    for (const aitagger::CatalogEntry& e : aitagger::BuiltinCatalog())
    {
        if (QString::fromStdString(e.id) == id)
        {
            downloadingId_ = id;
            downloadProgress_ = 0.0;
            lastError_.clear();
            Q_EMIT downloadChanged();
            downloader_->Start(e);
            return;
        }
    }
}

void AITaggerSettings::cancelDownload()
{
    downloader_->Cancel();
    downloadingId_.clear();
    downloadProgress_ = 0.0;
    Q_EMIT downloadChanged();
}

void AITaggerSettings::saveRule(
    const QString& folder, const QString& modelId, const QString& questions, double threshold, int frameBudget,
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
    for (const QString& raw : questions.split('\n', Qt::SkipEmptyParts))
    {
        const int sep = raw.indexOf("=>");
        if (sep < 0)
        {
            continue; // require "question => tag"
        }
        aitagger::RuleEntry e;
        e.question = raw.left(sep).trimmed().toStdString();
        e.tag = raw.mid(sep + 2).trimmed().toStdString();
        if (!e.question.empty() && !e.tag.empty())
        {
            rule.entries.push_back(std::move(e));
        }
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
