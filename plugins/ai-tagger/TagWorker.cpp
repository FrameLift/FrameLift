#include "TagWorker.h"

#include "SampleScheduler.h"
#include "TagStore.h"

#include <framelift/Log.h>
#include <framelift/platform/IAppWindow.h>
#include <framelift/services/IFrameSampler.h>

#include <chrono>
#include <filesystem>
#include <thread>

namespace aitagger
{
namespace
{
constexpr int kMaxInputSide = 768; // cap the frame handed to the model (memory/speed)
constexpr int kThrottleSleepMs = 120;

// Run status codes recorded on aitagger_runs.status.
constexpr int kStatusOk = 0;
constexpr int kStatusCancelled = 1;
constexpr int kStatusError = 2;

void FileStat(const std::string& path, long long& mtime, long long& size)
{
    std::error_code ec;
    const auto sz = std::filesystem::file_size(path, ec);
    size = ec ? 0 : static_cast<long long>(sz);
    const auto wt = std::filesystem::last_write_time(path, ec);
    mtime = ec ? 0 : std::chrono::duration_cast<std::chrono::seconds>(wt.time_since_epoch()).count();
}

void InputSize(int nativeW, int nativeH, int& outW, int& outH)
{
    if (nativeW <= 0 || nativeH <= 0)
    {
        outW = outH = 0; // let the sampler use native
        return;
    }
    const int longSide = nativeW > nativeH ? nativeW : nativeH;
    if (longSide <= kMaxInputSide)
    {
        outW = nativeW;
        outH = nativeH;
        return;
    }
    const double scale = static_cast<double>(kMaxInputSide) / longSide;
    outW = static_cast<int>(nativeW * scale);
    outH = static_cast<int>(nativeH * scale);
    if (outW < 1)
    {
        outW = 1;
    }
    if (outH < 1)
    {
        outH = 1;
    }
}
} // namespace

TagWorker::TagWorker(IFrameSampler* sampler, TagStore* store, BackendFactory factory)
{
    shared_->sampler = sampler;
    shared_->store = store;
    shared_->factory = std::move(factory);
}

TagWorker::~TagWorker()
{
    shared_->alive.store(false);
    shared_->cancel.store(true);
}

void TagWorker::Configure(IEventPump* events, uint32_t progressEvent, uint32_t doneEvent)
{
    shared_->events = events;
    shared_->progressEvent = progressEvent;
    shared_->doneEvent = doneEvent;
}

void TagWorker::SetThrottle(int nThreads)
{
    shared_->throttle.store(nThreads);
}

void TagWorker::Cancel()
{
    shared_->cancel.store(true);
}

std::vector<std::string> TagWorker::DrainCompleted()
{
    std::lock_guard lk(shared_->mtx);
    return std::exchange(shared_->completed, {});
}

TagProgress TagWorker::GetProgress() const
{
    std::lock_guard lk(shared_->mtx);
    return shared_->progress;
}

void TagWorker::Start(std::vector<TagJob> jobs)
{
    shared_->cancel.store(false);
    const uint64_t gen = shared_->latestGen.fetch_add(1) + 1;
    if (!shared_->events)
    {
        RunJobs(shared_, std::move(jobs), gen); // synchronous (headless/tests)
        return;
    }
    std::thread(
        [sh = shared_, jobs = std::move(jobs), gen]() mutable
        {
            RunJobs(sh, std::move(jobs), gen);
        }
    ).detach();
}

void TagWorker::RunSync(const std::vector<TagJob>& jobs)
{
    const uint64_t gen = shared_->latestGen.fetch_add(1) + 1;
    RunJobs(shared_, jobs, gen);
}

void TagWorker::RunJobs(const std::shared_ptr<Shared>& sh, std::vector<TagJob> jobs, uint64_t gen)
{
    if (!sh->alive.load() || !sh->factory)
    {
        return;
    }
    std::unique_ptr<IInferenceBackend> backend = sh->factory();
    std::string loadedModel;

    {
        std::lock_guard lk(sh->mtx);
        sh->progress = TagProgress{};
        sh->progress.running = true;
        sh->progress.filesTotal = static_cast<int>(jobs.size());
    }

    int lastThrottle = -999;
    int done = 0;
    for (const auto& job : jobs)
    {
        if (!sh->alive.load() || sh->cancel.load() || gen != sh->latestGen.load())
        {
            break;
        }

        {
            std::lock_guard lk(sh->mtx);
            sh->progress.currentFile = job.path;
        }

        // (Re)load the model only when it changes across jobs.
        if (backend && loadedModel != job.model.modelPath)
        {
            std::string err;
            if (!backend->LoadModel(job.model, err))
            {
                Log::Warn("AITagger: model load failed ({}): {}", job.model.modelPath, err);
                loadedModel.clear();
            }
            else
            {
                loadedModel = job.model.modelPath;
            }
        }

        const int throttle = sh->throttle.load();
        if (backend && throttle != lastThrottle)
        {
            backend->SetThreads(throttle > 0 ? throttle : job.model.nThreads);
            lastThrottle = throttle;
        }

        int status = kStatusOk;
        if (loadedModel.empty())
        {
            status = kStatusError;
            // Still record an (empty) run so the file isn't retried forever in a loop.
            const long long fileId = sh->store ? sh->store->UpsertFile(job.path, 0, 0) : 0;
            const long long runId = sh->store ? sh->store->BeginRun(fileId, job.model.modelId, job.ruleId) : 0;
            if (sh->store)
            {
                sh->store->FinishRun(runId, 0, status);
            }
        }
        else
        {
            (void)TagOneFile(sh, backend.get(), job, status);
        }

        ++done;
        {
            std::lock_guard lk(sh->mtx);
            sh->progress.filesDone = done;
            sh->completed.push_back(job.path);
        }
        if (sh->events && sh->alive.load())
        {
            sh->events->PushCustomEvent(sh->doneEvent);
        }
    }

    if (backend)
    {
        backend->UnloadModel();
    }
    {
        std::lock_guard lk(sh->mtx);
        sh->progress.running = false;
        sh->progress.currentFile.clear();
    }
    if (sh->events && sh->alive.load())
    {
        sh->events->PushCustomEvent(sh->progressEvent);
    }
}

int TagWorker::TagOneFile(const std::shared_ptr<Shared>& sh, IInferenceBackend* backend, const TagJob& job, int& status)
{
    status = kStatusOk;
    IFrameSampler* sampler = sh->sampler;
    if (!sampler)
    {
        status = kStatusError;
        return 0;
    }
    void* session = sampler->Open(job.path.c_str());
    if (!session)
    {
        status = kStatusError;
        return 0;
    }

    int nativeW = 0;
    int nativeH = 0;
    sampler->NativeSize(session, &nativeW, &nativeH);
    const double duration = sampler->DurationSec(session);
    int outW = 0;
    int outH = 0;
    InputSize(nativeW, nativeH, outW, outH);
    const int bufW = outW > 0 ? outW : nativeW;
    const int bufH = outH > 0 ? outH : nativeH;

    std::vector<BackendQuestion> questions;
    std::vector<float> thresholds;
    questions.reserve(job.entries.size());
    thresholds.reserve(job.entries.size());
    for (const auto& e : job.entries)
    {
        questions.push_back(BackendQuestion{e.question, e.tag});
        thresholds.push_back(e.threshold >= 0.0f ? e.threshold : job.ruleThreshold);
    }

    ConvergenceTracker tracker(thresholds, ConvergenceTracker::Params{});
    const SamplePlan plan = BuildSamplePlan(duration, job.frameBudget);

    std::vector<std::uint8_t> buf(static_cast<std::size_t>(bufW) * bufH * 4);
    std::vector<float> yesProb;
    int framesDone = 0;
    int genStart = 0;

    {
        std::lock_guard lk(sh->mtx);
        sh->progress.framesTotal = static_cast<int>(plan.timestamps.size());
        sh->progress.framesDone = 0;
    }

    for (int genEnd : plan.generationEnd)
    {
        for (int i = genStart; i < genEnd; ++i)
        {
            if (!sh->alive.load() || sh->cancel.load())
            {
                status = kStatusCancelled;
                sampler->Close(session);
                return framesDone;
            }
            double actual = 0.0;
            if (!sampler->ReadFrameRGBA(
                    session, plan.timestamps[i], outW, outH, buf.data(), static_cast<int>(buf.size()), &actual
                ))
            {
                continue; // skip an undecodable sample
            }
            std::string err;
            if (!backend->EvaluateFrame(buf.data(), bufW, bufH, questions, yesProb, err))
            {
                Log::Warn("AITagger: inference failed on {}: {}", job.path, err);
                status = kStatusError;
                sampler->Close(session);
                return framesDone;
            }
            tracker.Observe(yesProb);
            ++framesDone;
            {
                std::lock_guard lk(sh->mtx);
                sh->progress.framesDone = framesDone;
            }
            if (sh->events && sh->alive.load())
            {
                sh->events->PushCustomEvent(sh->progressEvent);
            }
            if (const int t = sh->throttle.load(); t > 0)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(kThrottleSleepMs));
            }
        }
        genStart = genEnd;
        if (tracker.AllSettled())
        {
            break;
        }
    }
    sampler->Close(session);

    std::vector<TagResult> results;
    results.reserve(job.entries.size());
    for (int q = 0; q < static_cast<int>(job.entries.size()); ++q)
    {
        TagResult r;
        r.tag = job.entries[q].tag;
        r.confidence = tracker.MaxConf(q);
        r.modelId = job.model.modelId;
        r.present = r.confidence >= thresholds[q];
        results.push_back(std::move(r));
    }

    if (sh->store)
    {
        long long mtime = 0;
        long long size = 0;
        FileStat(job.path, mtime, size);
        const long long fileId = sh->store->UpsertFile(job.path, mtime, size);
        const long long runId = sh->store->BeginRun(fileId, job.model.modelId, job.ruleId);
        sh->store->WriteTags(fileId, runId, job.model.modelId, results);
        sh->store->FinishRun(runId, framesDone, status);
    }
    return framesDone;
}

} // namespace aitagger
