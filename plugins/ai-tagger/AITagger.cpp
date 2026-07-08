#include "AITagger.h"

#include <framelift/Log.h>
#include <framelift/platform/IAppWindow.h>
#include <framelift/services/IFrameSampler.h>

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QDirIterator>
#include <QtCore/QFileInfo>

#include <cstring>

namespace
{
// Copy `s` into buf/cap (buf may be null to query length); returns full length excl NUL.
int CopyOut(const std::string& s, char* buf, int cap) noexcept
{
    const int len = static_cast<int>(s.size());
    if (buf && cap > 0)
    {
        const int n = len < cap - 1 ? len : cap - 1;
        std::memcpy(buf, s.data(), static_cast<std::size_t>(n));
        buf[n] = '\0';
    }
    return len;
}
} // namespace

AITagger::AITagger() = default;
AITagger::~AITagger() = default;

void AITagger::OnInstall(IModuleContext& ctx)
{
    store_ = std::make_unique<TagStore>(ctx.GetService<IMediaStore>());
    store_->EnsureSchema();
    sampler_ = ctx.GetService<IFrameSampler>();

    worker_ = std::make_unique<aitagger::TagWorker>(
        sampler_, store_.get(),
        []
        {
            return aitagger::CreateLlamaBackend();
        }
    );

    if (auto* events = ctx.GetService<IEventPump>())
    {
        progressEvent_ = events->RegisterCustomEventType();
        doneEvent_ = events->RegisterCustomEventType();
        worker_->Configure(events, progressEvent_, doneEvent_);
    }

    ctx.RegisterService<IMediaTags>(this);

    if (!sampler_)
    {
        Log::Warn("AITagger: IFrameSampler unavailable; tagging disabled");
    }
}

void AITagger::ConfigureForTest(IMediaStore* store, IFrameSampler* sampler, aitagger::TagWorker::BackendFactory factory)
{
    store_ = std::make_unique<TagStore>(store);
    store_->EnsureSchema();
    sampler_ = sampler;
    worker_ = std::make_unique<aitagger::TagWorker>(sampler_, store_.get(), std::move(factory));
}

bool AITagger::ResolveModel(const std::string& modelId, aitagger::ModelSpec& spec) const
{
    const QString dir = QCoreApplication::applicationDirPath() + "/models/";
    const QString model = dir + QString::fromStdString(modelId) + ".gguf";
    const QString mmproj = dir + QString::fromStdString(modelId) + ".mmproj.gguf";
    if (!QFileInfo::exists(model) || !QFileInfo::exists(mmproj))
    {
        return false;
    }
    spec.modelPath = model.toStdString();
    spec.mmprojPath = mmproj.toStdString();
    spec.modelId = modelId;
    return true;
}

bool AITagger::BuildJob(const std::string& path, aitagger::TagJob& job) const
{
    if (!store_)
    {
        return false;
    }
    aitagger::TagRule rule;
    if (!store_->RuleForFile(path, rule) || rule.entries.empty())
    {
        return false;
    }
    aitagger::ModelSpec spec;
    if (!ResolveModel(rule.modelId, spec))
    {
        Log::Warn("AITagger: model '{}' not installed; skipping {}", rule.modelId, path);
        return false;
    }
    job.path = path;
    job.ruleId = rule.id;
    job.model = std::move(spec);
    job.entries = rule.entries;
    job.ruleThreshold = rule.threshold;
    job.frameBudget = rule.frameBudget;
    return true;
}

void AITagger::tagFile(const QString& path)
{
    if (!worker_)
    {
        return;
    }
    aitagger::TagJob job;
    if (BuildJob(path.toStdString(), job))
    {
        worker_->Start({std::move(job)});
        Q_EMIT progressChanged();
    }
}

void AITagger::tagFolder(const QString& dir)
{
    if (!worker_)
    {
        return;
    }
    static const QStringList kVideoGlobs = {"*.mp4", "*.mkv",  "*.avi", "*.mov", "*.wmv",
                                            "*.flv", "*.webm", "*.m4v", "*.mpg", "*.mpeg"};
    std::vector<aitagger::TagJob> jobs;
    QDirIterator it(dir, kVideoGlobs, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        const QString file = it.next();
        const std::string path = file.toStdString();
        const QFileInfo fi(file);
        if (!store_->NeedsTagging(
                path, static_cast<long long>(fi.lastModified().toSecsSinceEpoch()), static_cast<long long>(fi.size())
            ))
        {
            continue;
        }
        aitagger::TagJob job;
        if (BuildJob(path, job))
        {
            jobs.push_back(std::move(job));
        }
    }
    if (!jobs.empty())
    {
        worker_->Start(std::move(jobs));
        Q_EMIT progressChanged();
    }
}

void AITagger::cancel()
{
    if (worker_)
    {
        worker_->Cancel();
    }
}

void AITagger::PublishCompleted()
{
    if (!worker_ || !ctx_)
    {
        return;
    }
    for (const std::string& path : worker_->DrainCompleted())
    {
        ctx_->Publish(MediaTagsUpdatedEvent{path.c_str()});
    }
}

bool AITagger::HandleEvent(const AppEvent& e)
{
    if (e.type == AppEventType::Custom)
    {
        const uint32_t et = e.AsCustom().eventType;
        if (doneEvent_ != 0 && et == doneEvent_)
        {
            PublishCompleted();
            Q_EMIT progressChanged();
            return false;
        }
        if (progressEvent_ != 0 && et == progressEvent_)
        {
            Q_EMIT progressChanged();
            return false;
        }
    }
    return ModuleBase::HandleEvent(e);
}

bool AITagger::IsRunning() const
{
    return worker_ && worker_->GetProgress().running;
}

QString AITagger::CurrentFile() const
{
    return worker_ ? QString::fromStdString(worker_->GetProgress().currentFile) : QString();
}

int AITagger::FilesDone() const
{
    return worker_ ? worker_->GetProgress().filesDone : 0;
}

int AITagger::FilesTotal() const
{
    return worker_ ? worker_->GetProgress().filesTotal : 0;
}

// ── IMediaTags ─────────────────────────────────────────────────────────────────

int AITagger::GetTagCount(const char* path) const noexcept
{
    if (!store_ || !path)
    {
        return 0;
    }
    return store_->TagCount(path);
}

int AITagger::GetTag(
    const char* path, int index, char* buf, int cap, float* confidence, char* modelBuf, int modelCap
) const noexcept
{
    if (!store_ || !path)
    {
        return -1;
    }
    aitagger::TagResult r;
    if (!store_->TagAt(path, index, r))
    {
        return -1;
    }
    if (confidence)
    {
        *confidence = r.confidence;
    }
    if (modelBuf)
    {
        (void)CopyOut(r.modelId, modelBuf, modelCap);
    }
    return CopyOut(r.tag, buf, cap);
}

bool AITagger::HasTag(const char* path, const char* tag, float minConfidence) const noexcept
{
    if (!store_ || !path || !tag)
    {
        return false;
    }
    return store_->HasTag(path, tag, minConfidence);
}
