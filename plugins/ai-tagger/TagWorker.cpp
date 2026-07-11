#include "TagWorker.h"

#include "SampleScheduler.h"
#include "TagStore.h"

#include <framelift/Log.h>
#include <framelift/platform/IAppWindow.h>
#include <framelift/services/IFrameSampler.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <functional>
#include <thread>

namespace aitagger
{
namespace
{
constexpr int kThrottleSleepMs = 120;
constexpr int kThumbnailLongSide = 96;
constexpr int kDetailLongSide = 768;
constexpr int kDetailSourceLongSide = 1536;

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

VisualSignature MakeSignature(const std::uint8_t* rgba, int width, int height)
{
    VisualSignature signature{};
    std::array<int, 16> counts{};
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const int cellX = std::min(3, x * 4 / std::max(1, width));
            const int cellY = std::min(3, y * 4 / std::max(1, height));
            const int cell = cellY * 4 + cellX;
            const auto* pixel = rgba + (static_cast<std::size_t>(y) * width + x) * 4;
            signature[cell] += (0.2126f * pixel[0] + 0.7152f * pixel[1] + 0.0722f * pixel[2]) / 255.0f;
            ++counts[cell];
        }
    }
    for (int i = 0; i < 16; ++i)
    {
        if (counts[i] > 0)
        {
            signature[i] /= counts[i];
        }
    }
    return signature;
}

struct RgbaImage
{
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;
};

RgbaImage CropAndScale(const std::uint8_t* source, int sourceW, int sourceH, int x0, int y0, int cropW, int cropH)
{
    RgbaImage out;
    InputSize(cropW, cropH, kDetailLongSide, out.width, out.height);
    out.pixels.resize(static_cast<std::size_t>(out.width) * out.height * 4);
    for (int y = 0; y < out.height; ++y)
    {
        const int sy = std::clamp(y0 + y * cropH / std::max(1, out.height), 0, sourceH - 1);
        for (int x = 0; x < out.width; ++x)
        {
            const int sx = std::clamp(x0 + x * cropW / std::max(1, out.width), 0, sourceW - 1);
            const auto* src = source + (static_cast<std::size_t>(sy) * sourceW + sx) * 4;
            auto* dst = out.pixels.data() + (static_cast<std::size_t>(y) * out.width + x) * 4;
            std::copy_n(src, 4, dst);
        }
    }
    return out;
}

std::vector<RgbaImage> DetailCrops(const std::uint8_t* rgba, int width, int height)
{
    const int cropW = std::max(1, width * 2 / 3);
    const int cropH = std::max(1, height * 2 / 3);
    const int maxX = width - cropW;
    const int maxY = height - cropH;
    const std::array<std::pair<int, int>, 5> origins{
        {{0, 0}, {maxX, 0}, {0, maxY}, {maxX, maxY}, {maxX / 2, maxY / 2}}
    };
    std::vector<RgbaImage> out;
    out.reserve(origins.size());
    for (const auto& [x, y] : origins)
    {
        out.push_back(CropAndScale(rgba, width, height, x, y, cropW, cropH));
    }
    return out;
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

    const int total = static_cast<int>(jobs.size());
    {
        std::lock_guard lk(sh->mtx);
        sh->progress = TagProgress{};
        sh->progress.running = true;
        sh->progress.filesTotal = total;
    }
    Log::Debug("AITagger: tagging run started (gen {}) — {} file(s)", gen, total);

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
        Log::Debug("AITagger: [{}/{}] tagging {} (model {})", done + 1, total, job.path, job.model.modelId);

        // (Re)select the shared model only when it changes across jobs. Production
        // jobs carry a model id; test backends may still use a synthetic path.
        const std::string modelKey = !job.model.modelId.empty() ? job.model.modelId : job.model.modelPath;
        if (backend && loadedModel != modelKey)
        {
            std::string err;
            if (!backend->LoadModel(job.model, err))
            {
                Log::Warn("AITagger: model selection failed ({}): {}", modelKey, err);
                loadedModel.clear();
            }
            else
            {
                loadedModel = modelKey;
                Log::Debug("AITagger: selected model {}", modelKey);
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
            // Record the failed attempt for diagnostics. NeedsTagging only accepts a
            // successful matching fingerprint, so a later run can retry it.
            const long long fileId = sh->store ? sh->store->UpsertFile(job.path, 0, 0) : 0;
            const long long runId =
                sh->store ? sh->store->BeginRun(fileId, job.model.modelId, job.ruleId, job.fingerprint) : 0;
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
    const bool cancelled = sh->cancel.load();
    Log::Debug(
        "AITagger: tagging run finished (gen {}) — {}/{} file(s){}", gen, done, total, cancelled ? " (cancelled)" : ""
    );
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
    const auto startedAt = std::chrono::steady_clock::now();
    status = kStatusOk;
    IFrameSampler* sampler = sh->sampler;
    if (!sampler)
    {
        Log::Warn("AITagger: no frame sampler available; cannot tag {}", job.path);
        status = kStatusError;
        return 0;
    }
    void* session = sampler->Open(job.path.c_str());
    if (!session)
    {
        Log::Warn("AITagger: failed to open {} for frame sampling", job.path);
        status = kStatusError;
        return 0;
    }

    int nativeW = 0;
    int nativeH = 0;
    if (!sampler->NativeSize(session, &nativeW, &nativeH) || nativeW <= 0 || nativeH <= 0)
    {
        Log::Warn("AITagger: invalid native frame size for {}", job.path);
        status = kStatusError;
        sampler->Close(session);
        return 0;
    }
    const double duration = sampler->DurationSec(session);
    int outW = 0;
    int outH = 0;
    InputSize(nativeW, nativeH, job.maxInputSide, outW, outH);
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
    const SamplePlan candidates = BuildSamplePlan(duration, job.frameBudget);

    // Cheap thumbnail pass: preserve the midpoint candidate set (and therefore its
    // intentional edge avoidance), but rank candidates by visual novelty + temporal
    // separation before paying for VLM inference.
    const auto thumbnailStarted = std::chrono::steady_clock::now();
    int thumbW = 0;
    int thumbH = 0;
    InputSize(nativeW, nativeH, kThumbnailLongSide, thumbW, thumbH);
    std::vector<double> chronological = candidates.timestamps;
    std::ranges::sort(chronological);
    std::vector<double> validTimestamps;
    std::vector<VisualSignature> signatures;
    std::vector<std::uint8_t> thumbnail(static_cast<std::size_t>(thumbW) * thumbH * 4);
    for (double timestamp : chronological)
    {
        if (!sh->alive.load() || sh->cancel.load())
        {
            status = kStatusCancelled;
            sampler->Close(session);
            return 0;
        }
        double actual = 0.0;
        if (sampler->ReadFrameRGBA(
                session, timestamp, thumbW, thumbH, thumbnail.data(), static_cast<int>(thumbnail.size()), &actual
            ))
        {
            validTimestamps.push_back(timestamp);
            signatures.push_back(MakeSignature(thumbnail.data(), thumbW, thumbH));
        }
    }
    SamplePlan plan = RankAdaptiveSamples(validTimestamps, signatures, duration);
    if (plan.timestamps.empty())
    {
        Log::Warn("AITagger: no candidate thumbnails could be decoded for {}", job.path);
        status = kStatusError;
        sampler->Close(session);
        return 0;
    }
    const auto thumbnailMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - thumbnailStarted)
            .count();
    Log::Debug(
        "AITagger: {} — {}x{} native, {}x{} full-frame, {:.1f}s, {} question(s), {} adaptive frame(s) (budget {})",
        job.path, nativeW, nativeH, bufW, bufH, duration, questions.size(), plan.timestamps.size(), job.frameBudget
    );

    std::vector<std::uint8_t> buf(static_cast<std::size_t>(bufW) * bufH * 4);
    std::vector<float> yesProb;
    int framesDone = 0;
    int genStart = 0;
    int detailFrames = 0;
    long long inferenceMs = 0;
    long long decodeMs = 0;
    long long cropMs = 0;

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
            const auto decodeStarted = std::chrono::steady_clock::now();
            if (!sampler->ReadFrameRGBA(
                    session, plan.timestamps[i], outW, outH, buf.data(), static_cast<int>(buf.size()), &actual
                ))
            {
                continue; // skip an undecodable sample
            }
            decodeMs +=
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - decodeStarted)
                    .count();
            std::string err;
            const auto inferenceStarted = std::chrono::steady_clock::now();
            if (!backend->EvaluateFrame(buf.data(), bufW, bufH, questions, yesProb, err))
            {
                Log::Warn("AITagger: inference failed on {}: {}", job.path, err);
                status = kStatusError;
                sampler->Close(session);
                return framesDone;
            }
            inferenceMs += std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - inferenceStarted
            )
                               .count();
            tracker.Observe(yesProb, actual);
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

        // Detail analysis starts only after the initial seven diverse full frames.
        // HumanDetail always opts in; Auto opts in for unresolved mid-confidence
        // questions. FullFrame questions never consume crop evidence.
        if (framesDone >= 7 && detailFrames < 2)
        {
            std::vector<int> detailQuestionIndices;
            std::vector<BackendQuestion> detailQuestions;
            for (int q = 0; q < static_cast<int>(job.entries.size()); ++q)
            {
                const auto mode = job.entries[q].analysisMode;
                const bool autoDetail =
                    mode == AnalysisMode::Auto && !tracker.Present(q) && tracker.MaxConf(q) >= 0.25f;
                if (mode == AnalysisMode::HumanDetail || autoDetail)
                {
                    detailQuestionIndices.push_back(q);
                    detailQuestions.push_back(questions[q]);
                }
            }
            if (!detailQuestions.empty())
            {
                std::vector<std::pair<float, double>> evidence;
                for (int q : detailQuestionIndices)
                {
                    if (tracker.BestTimestamp(q) > 0.0)
                    {
                        evidence.emplace_back(tracker.MaxConf(q), tracker.BestTimestamp(q));
                    }
                }
                std::ranges::sort(evidence, std::greater{});
                std::vector<double> detailTimestamps;
                for (const auto& [confidence, timestamp] : evidence)
                {
                    (void)confidence;
                    if (std::ranges::find(detailTimestamps, timestamp) == detailTimestamps.end())
                    {
                        detailTimestamps.push_back(timestamp);
                    }
                    if (detailTimestamps.size() >= 2)
                    {
                        break;
                    }
                }
                for (double timestamp : plan.timestamps)
                {
                    if (detailTimestamps.size() >= 2)
                    {
                        break;
                    }
                    if (std::ranges::find(detailTimestamps, timestamp) == detailTimestamps.end())
                    {
                        detailTimestamps.push_back(timestamp);
                    }
                }
                for (double timestamp : detailTimestamps)
                {
                    if (detailFrames >= 2)
                    {
                        break;
                    }
                    int detailW = 0;
                    int detailH = 0;
                    InputSize(nativeW, nativeH, std::max(kDetailSourceLongSide, job.maxInputSide), detailW, detailH);
                    std::vector<std::uint8_t> detailSource(static_cast<std::size_t>(detailW) * detailH * 4);
                    double actual = 0.0;
                    const auto detailDecodeStarted = std::chrono::steady_clock::now();
                    if (!sampler->ReadFrameRGBA(
                            session, timestamp, detailW, detailH, detailSource.data(),
                            static_cast<int>(detailSource.size()), &actual
                        ))
                    {
                        continue;
                    }
                    decodeMs += std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - detailDecodeStarted
                    )
                                    .count();
                    ++detailFrames;
                    const auto cropStarted = std::chrono::steady_clock::now();
                    for (const RgbaImage& crop : DetailCrops(detailSource.data(), detailW, detailH))
                    {
                        std::vector<float> detailScores;
                        std::string err;
                        const auto inferenceStarted = std::chrono::steady_clock::now();
                        if (!backend->EvaluateFrame(
                                crop.pixels.data(), crop.width, crop.height, detailQuestions, detailScores, err
                            ))
                        {
                            Log::Warn("AITagger: detail inference failed on {}: {}", job.path, err);
                            status = kStatusError;
                            sampler->Close(session);
                            return framesDone;
                        }
                        inferenceMs += std::chrono::duration_cast<std::chrono::milliseconds>(
                                           std::chrono::steady_clock::now() - inferenceStarted
                        )
                                           .count();
                        std::vector<float> masked(job.entries.size(), -1.0f);
                        for (std::size_t i = 0; i < detailScores.size() && i < detailQuestionIndices.size(); ++i)
                        {
                            masked[detailQuestionIndices[i]] = detailScores[i];
                        }
                        tracker.Observe(masked, actual);
                    }
                    cropMs += std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now() - cropStarted
                    )
                                  .count();
                }
            }
        }
        if (tracker.AllSettled())
        {
            Log::Debug("AITagger: {} converged early after {} frame(s)", job.path, framesDone);
            break;
        }
    }
    sampler->Close(session);

    if (framesDone == 0)
    {
        Log::Warn("AITagger: no full-resolution samples could be decoded for {}", job.path);
        status = kStatusError;
        return 0;
    }

    std::vector<TagResult> results;
    results.reserve(job.entries.size());
    for (int q = 0; q < static_cast<int>(job.entries.size()); ++q)
    {
        TagResult r;
        r.tag = job.entries[q].tag;
        r.confidence = tracker.MaxConf(q);
        r.modelId = job.model.modelId;
        r.present = tracker.Present(q);
        r.supportCount = tracker.SupportCount(q);
        r.bestTimestamp = tracker.BestTimestamp(q);
        results.push_back(std::move(r));
    }

    int presentCount = 0;
    for (const TagResult& r : results)
    {
        if (r.present)
        {
            ++presentCount;
        }
    }
    Log::Debug(
        "AITagger: {} done — {} frame(s) sampled, {}/{} tag(s) present", job.path, framesDone, presentCount,
        results.size()
    );

    if (sh->store)
    {
        long long mtime = 0;
        long long size = 0;
        FileStat(job.path, mtime, size);
        const long long fileId = sh->store->UpsertFile(job.path, mtime, size);
        const long long runId = sh->store->BeginRun(fileId, job.model.modelId, job.ruleId, job.fingerprint);
        sh->store->WriteTags(fileId, runId, job.model.modelId, results);
        sh->store->FinishRun(runId, framesDone, status);
    }
    const auto totalMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startedAt).count();
    Log::Debug(
        "AITagger: {} timings — thumbnails {}ms, selected decode {}ms, inference {}ms, detail {}ms, total {}ms",
        job.path, thumbnailMs, decodeMs, inferenceMs, cropMs, totalMs
    );
    return framesDone;
}

} // namespace aitagger
