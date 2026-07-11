#include "AITagger.h"

#include "AITaggerSettings.h"
#include "HostAIBackend.h"
#include "SampleScheduler.h"

#include <framelift/ContextMenu.h>
#include <framelift/ContextMenuHelpers.h>
#include <framelift/Log.h>
#include <framelift/platform/IAppWindow.h>
#include <framelift/services/IAIImageQuestionScoring.h>
#include <framelift/services/IFrameSampler.h>
#include <framelift/services/ISettingsStore.h>

#include <QtCore/QDir>
#include <QtCore/QDirIterator>
#include <QtCore/QFileInfo>

#include <algorithm>
#include <cstring>
#include <filesystem>

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

// Human-readable reason a file was not enqueued (for the "nothing to tag" logs).
const char* SkipReason(AITagger::JobSkip why)
{
    switch (why)
    {
    case AITagger::JobSkip::NoStore:
        return "tag store unavailable";
    case AITagger::JobSkip::NoRule:
        return "no folder rule covers it";
    case AITagger::JobSkip::NoQuestions:
        return "the matching rule has no questions";
    case AITagger::JobSkip::ModelMissing:
        return "the rule's model is not installed";
    case AITagger::JobSkip::None:
        break;
    }
    return "no untagged files";
}
} // namespace

AITagger::AITagger()
{
    connect(
        &dirWatcher_, &QFileSystemWatcher::directoryChanged, this,
        [this](const QString&)
        {
            if (auto* events = ctx_ && dirChangedEvent_ ? ctx_->GetService<IEventPump>() : nullptr)
            {
                events->PushCustomEvent(dirChangedEvent_);
            }
        }
    );
}

AITagger::~AITagger() = default;

void AITagger::OnInstall(IModuleContext& ctx)
{
    store_ = std::make_unique<TagStore>(ctx.GetService<IMediaStore>());
    store_->EnsureSchema();
    sampler_ = ctx.GetService<IFrameSampler>();
    inference_ = ctx.GetService<IAIInference>();
    scoring_ = ctx.GetService<IAIImageQuestionScoring>();
    models_ = ctx.GetService<IAIModelManager>();
    settingsStore_ = ctx.GetService<ISettingsStore>();
    if (settingsStore_)
    {
        const int configured = settingsStore_->GetSettingInt("aitagger.maxInputSide");
        maxInputSide_ = configured > 0 ? std::clamp(configured, 256, 2048) : 768;
    }

    worker_ = std::make_unique<aitagger::TagWorker>(
        sampler_, store_.get(),
        [scoring = scoring_, inference = inference_, models = models_]
        {
            return aitagger::CreateHostAIBackend(scoring, inference, models);
        }
    );

    if (auto* events = ctx.GetService<IEventPump>())
    {
        progressEvent_ = events->RegisterCustomEventType();
        doneEvent_ = events->RegisterCustomEventType();
        dirChangedEvent_ = events->RegisterCustomEventType();
        worker_->Configure(events, progressEvent_, doneEvent_);
    }

    ctx.RegisterService<IMediaTags>(this);

    framelift::Subscribe<FileOpenedEvent>(
        ctx,
        [this](const FileOpenedEvent& e)
        {
            currentFile_ = e.path ? e.path : "";
        }
    );

    if (auto* pages = ctx.GetService<ISettingsPageRegistry>())
    {
        settingsPage_ = std::make_unique<AITaggerSettings>(*this);
        pages->RegisterSettingsPage(
            "aitagger", "AI Tagger", "qrc:/qt/qml/FrameLift/Plugins/AITagger/AITaggerSettings.qml", settingsPage_.get(),
            320
        );
    }

    if (auto* menu = ctx.GetService<ContextMenu>())
    {
        framelift::AddSection(
            *menu,
            [this](ContextMenu& m)
            {
                framelift::AddItem(
                    m, "Tag this video", "aitagger.tagFile",
                    [this]
                    {
                        tagFile(QString::fromStdString(currentFile_));
                    }
                );
                framelift::AddItem(
                    m, "Tag this folder", "aitagger.tagFolder",
                    [this]
                    {
                        if (!currentFile_.empty())
                        {
                            tagFolder(
                                QString::fromStdString(std::filesystem::path(currentFile_).parent_path().string())
                            );
                        }
                    }
                );
            }
        );
    }

    ArmWatchers();

    if (!sampler_)
    {
        Log::Warn("AITagger: IFrameSampler unavailable; tagging disabled");
    }
    if ((!scoring_ && !inference_) || !models_)
    {
        Log::Warn("AITagger: shared AI host services unavailable; tagging disabled");
    }
}

void AITagger::SetMaxInputSide(int value)
{
    value = std::clamp(value, 256, 2048);
    value = ((value + 32) / 64) * 64;
    if (maxInputSide_ == value)
    {
        return;
    }
    maxInputSide_ = value;
    if (settingsStore_)
    {
        settingsStore_->CommitSettingInt("aitagger.maxInputSide", maxInputSide_);
        settingsStore_->SaveSettings();
    }
}

void AITagger::OnRulesChanged()
{
    ArmWatchers();
}

void AITagger::ArmWatchers()
{
    if (!dirWatcher_.directories().isEmpty())
    {
        dirWatcher_.removePaths(dirWatcher_.directories());
    }
    if (!store_)
    {
        return;
    }
    QStringList dirs;
    for (const aitagger::TagRule& r : store_->ListRules())
    {
        if (r.watch && QFileInfo::exists(QString::fromStdString(r.folder)))
        {
            dirs << QString::fromStdString(r.folder);
        }
    }
    if (!dirs.isEmpty())
    {
        dirWatcher_.addPaths(dirs);
    }
}

void AITagger::HandleMediaEvent(const MediaEvent& e)
{
    (void)e; // playback-aware throttling is centralized in the host AI scheduler
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
    if (!models_ || !models_->IsInstalled(modelId.c_str()))
    {
        return false;
    }
    spec.modelId = modelId;
    if (scoring_)
    {
        const int length = scoring_->GetModelRevision(modelId.c_str(), nullptr, 0);
        if (length > 0)
        {
            std::vector<char> revision(static_cast<std::size_t>(length) + 1);
            scoring_->GetModelRevision(modelId.c_str(), revision.data(), static_cast<int>(revision.size()));
            spec.revision = revision.data();
        }
    }
    return true;
}

bool AITagger::BuildJob(const std::string& path, aitagger::TagJob& job, JobSkip& why) const
{
    why = JobSkip::None;
    if (!store_)
    {
        why = JobSkip::NoStore;
        return false;
    }
    aitagger::TagRule rule;
    if (!store_->RuleForFile(path, rule))
    {
        why = JobSkip::NoRule;
        Log::Debug("AITagger: no rule covers {}", path);
        return false;
    }
    if (rule.entries.empty())
    {
        why = JobSkip::NoQuestions;
        Log::Debug("AITagger: rule '{}' has no questions; skipping {}", rule.folder, path);
        return false;
    }
    aitagger::ModelSpec spec;
    if (!ResolveModel(rule.modelId, spec))
    {
        why = JobSkip::ModelMissing;
        Log::Warn("AITagger: model '{}' not installed; skipping {}", rule.modelId, path);
        return false;
    }
    job.path = path;
    job.ruleId = rule.id;
    job.model = std::move(spec);
    job.entries = rule.entries;
    job.ruleThreshold = rule.threshold;
    job.frameBudget = rule.frameBudget;
    job.maxInputSide = maxInputSide_;
    job.fingerprint = aitagger::BuildTaggingFingerprint(
        job.model.modelId, job.model.revision, job.entries, job.ruleThreshold, job.frameBudget, job.maxInputSide
    );
    return true;
}

void AITagger::tagFile(const QString& path)
{
    const std::string p = path.toStdString();
    Log::Debug("AITagger: Tag file — {}", p);
    if (!worker_)
    {
        Log::Warn("AITagger: worker unavailable; cannot tag {}", p);
        return;
    }
    aitagger::TagJob job;
    JobSkip why = JobSkip::None;
    if (BuildJob(p, job, why))
    {
        Log::Debug("AITagger: queued {}", p);
        worker_->Start({std::move(job)});
        Q_EMIT progressChanged();
        return;
    }
    Log::Warn("AITagger: nothing to tag for {} ({})", p, SkipReason(why));
}

void AITagger::tagFolder(const QString& dir)
{
    const std::string dirStr = dir.toStdString();
    Log::Debug("AITagger: Tag now — scanning {}", dirStr);
    if (!worker_)
    {
        Log::Warn("AITagger: worker unavailable; cannot tag {}", dirStr);
        return;
    }
    static const QStringList kVideoGlobs = {"*.mp4", "*.mkv",  "*.avi", "*.mov", "*.wmv",
                                            "*.flv", "*.webm", "*.m4v", "*.mpg", "*.mpeg"};
    std::vector<aitagger::TagJob> jobs;
    int scanned = 0;
    int alreadyTagged = 0;
    int noRule = 0;
    int noQuestions = 0;
    int modelMissing = 0;
    QDirIterator it(dir, kVideoGlobs, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        const QString file = it.next();
        const std::string path = file.toStdString();
        const QFileInfo fi(file);
        ++scanned;
        aitagger::TagJob job;
        JobSkip why = JobSkip::None;
        if (BuildJob(path, job, why))
        {
            if (!store_->NeedsTagging(
                    path, static_cast<long long>(fi.lastModified().toSecsSinceEpoch()),
                    static_cast<long long>(fi.size()), job.fingerprint
                ))
            {
                ++alreadyTagged;
                continue;
            }
            jobs.push_back(std::move(job));
            continue;
        }
        switch (why)
        {
        case JobSkip::NoRule:
            ++noRule;
            break;
        case JobSkip::NoQuestions:
            ++noQuestions;
            break;
        case JobSkip::ModelMissing:
            ++modelMissing;
            break;
        default:
            break;
        }
    }

    Log::Debug(
        "AITagger: {} — {} video(s) scanned, {} already tagged, {} no-rule, {} no-questions, {} model-missing, {} "
        "queued",
        dirStr, scanned, alreadyTagged, noRule, noQuestions, modelMissing, jobs.size()
    );

    if (jobs.empty())
    {
        // Name the most useful reason so it's visible without debug logging.
        const char* reason = scanned == 0               ? "no video files found under the folder"
                             : modelMissing > 0         ? "the rule's model is not installed"
                             : noQuestions > 0          ? "the matching rule has no questions"
                             : noRule > 0               ? "no folder rule covers these files"
                             : alreadyTagged == scanned ? "all files are already tagged"
                                                        : "nothing to tag";
        Log::Warn("AITagger: nothing queued for {} ({})", dirStr, reason);
        return;
    }

    Log::Debug("AITagger: starting run — {} file(s)", jobs.size());
    worker_->Start(std::move(jobs));
    Q_EMIT progressChanged();
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
        if (dirChangedEvent_ != 0 && et == dirChangedEvent_)
        {
            // A watched folder changed: enqueue any new untagged files (tagFolder
            // skips files already tagged with a matching mtime/size).
            if (store_)
            {
                for (const aitagger::TagRule& r : store_->ListRules())
                {
                    if (r.watch)
                    {
                        tagFolder(QString::fromStdString(r.folder));
                    }
                }
            }
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
