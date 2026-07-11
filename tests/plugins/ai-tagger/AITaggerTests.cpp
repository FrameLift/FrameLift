#include "HostAIBackend.h"
#include "InferenceBackend.h"
#include "SampleScheduler.h"
#include "TagStore.h"
#include "TagWorker.h"

#include "MediaStoreImpl.h"
#include "QtTestRunner.h"
#include "TempIni.h"
#include "fakes/FakeFrameSampler.h"

#include <framelift/services.h>

#include <QtTest/QtTest>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace aitagger;

namespace
{
// A real Qt SQL-backed store over a unique temp directory (WAL adds -wal/-shm siblings).
class TempDb
{
public:
    ~TempDb()
    {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

    [[nodiscard]] QString qstr() const
    {
        return QString::fromStdString((dir / "media.db").string());
    }

    std::filesystem::path dir = UniqueTempPath();
};

// Fake backend returning fixed yes-probabilities for every frame.
class FakeBackend final : public IInferenceBackend
{
public:
    std::vector<float> probs;

    explicit FakeBackend(std::vector<float> p) : probs(std::move(p))
    {
    }

    bool LoadModel(const ModelSpec&, std::string&) override
    {
        return true;
    }

    void UnloadModel() override
    {
    }

    void SetThreads(int) override
    {
    }

    bool EvaluateFrame(
        const std::uint8_t*, int, int, const std::vector<BackendQuestion>& qs, std::vector<float>& out, std::string&
    ) override
    {
        out.assign(qs.size(), 0.0f);
        for (std::size_t i = 0; i < qs.size() && i < probs.size(); ++i)
        {
            out[i] = probs[i];
        }
        return true;
    }
};

class FakeSharedModels final : public IAIModelManager
{
public:
    void Enumerate(AIModelVisitor, void*) const noexcept override
    {
    }

    bool IsInstalled(const char* id) const noexcept override
    {
        return id && std::string(id) == "fake";
    }

    std::uint64_t Install(const char*, AIModelTransferCallback, void*) noexcept override
    {
        return 0;
    }

    std::uint64_t Import(
        const char*, const char*, const char*, const char*, AIModelTransferCallback, void*
    ) noexcept override
    {
        return 0;
    }

    void CancelTransfer(std::uint64_t) noexcept override
    {
    }

    bool Remove(const char*) noexcept override
    {
        return false;
    }

    int GetLoadedModelLimit() const noexcept override
    {
        return 2;
    }

    void SetLoadedModelLimit(int) noexcept override
    {
    }
};

class FakeSharedInference final : public IAIInference
{
public:
    struct Client
    {
        AICompletionCallback completion;
        void* ud;
    };

    void* CreateClient(AIProgressCallback, AICompletionCallback completion, void* ud) noexcept override
    {
        client = {completion, ud};
        return &client;
    }

    void DestroyClient(void*) noexcept override
    {
    }

    std::uint64_t Submit(void*, const AIInferenceRequest* request) noexcept override
    {
        sawBackground = request && request->priority == AIRequestPriority::Background;
        const float values[] = {0.8f, 0.2f};
        const AIInferenceResult result{1, AIJobState::Completed, "", values, 2, ""};
        client.completion(&result, client.ud);
        return 1;
    }

    void Cancel(void*, std::uint64_t) noexcept override
    {
    }

    Client client{};
    bool sawBackground = false;
};

TagJob MakeJob(const std::string& path, std::vector<RuleEntry> entries, float threshold, int budget)
{
    TagJob j;
    j.path = path;
    j.model.modelPath = "fake-model";
    j.model.modelId = "fake";
    j.entries = std::move(entries);
    j.ruleThreshold = threshold;
    j.frameBudget = budget;
    return j;
}
} // namespace

class AITaggerTest final : public QObject
{
    Q_OBJECT

    // One store for the whole suite. MediaStoreImpl keeps a per-thread connection in
    // QThreadStorage; creating several instances in one process can hand a new instance
    // the previous one's stale (removed) connection (QThreadStorage id recycling), so we
    // use a single store and clear the tables between tests instead.
    std::unique_ptr<TempDb> db_;
    std::unique_ptr<MediaStoreImpl> store_;
    std::unique_ptr<TagStore> ts_;

    void ClearTables()
    {
        for (const char* t :
             {"aitagger_files", "aitagger_runs", "aitagger_tags", "aitagger_rules", "aitagger_rule_entries"})
        {
            (void)store_->Exec((std::string("DELETE FROM ") + t).c_str());
        }
    }

private Q_SLOTS:

    void initTestCase()
    {
        db_ = std::make_unique<TempDb>();
        store_ = std::make_unique<MediaStoreImpl>(db_->qstr());
        ts_ = std::make_unique<TagStore>(store_.get());
        ts_->EnsureSchema();
        QVERIFY(store_->Exec("SELECT 1")); // fail early if the SQLite driver is unavailable
    }

    void init()
    {
        ClearTables();
    }

    void cleanupTestCase()
    {
        ts_.reset();
        store_.reset();
        db_.reset();
    }

    // ── SampleScheduler ──────────────────────────────────────────────────────
    void PlanBudgetAndBoundaries()
    {
        const SamplePlan p = BuildSamplePlan(20.0, 31);
        QCOMPARE(static_cast<int>(p.timestamps.size()), 31);
        const std::vector<int> expectedEnds{1, 3, 7, 15, 31};
        QCOMPARE(p.generationEnd, expectedEnds);
        QCOMPARE(p.timestamps[0], 10.0); // d/2
        QCOMPARE(p.timestamps[1], 5.0);  // d/4
        QCOMPARE(p.timestamps[2], 15.0); // 3d/4
    }

    void PlanRespectsSmallBudget()
    {
        const SamplePlan p = BuildSamplePlan(20.0, 5);
        QCOMPARE(static_cast<int>(p.timestamps.size()), 5);
    }

    void PlanZeroDurationYieldsOne()
    {
        const SamplePlan p = BuildSamplePlan(0.0, 31);
        QCOMPARE(static_cast<int>(p.timestamps.size()), 1);
        QCOMPARE(p.timestamps[0], 0.0);
    }

    void AdaptiveRankingPreservesInteriorCandidateSet()
    {
        const SamplePlan source = BuildSamplePlan(20.0, 31);
        std::vector<VisualSignature> signatures(source.timestamps.size());
        const SamplePlan ranked = RankAdaptiveSamples(source.timestamps, signatures, 20.0);
        QCOMPARE(static_cast<int>(ranked.timestamps.size()), 31);
        QVERIFY(*std::ranges::min_element(ranked.timestamps) > 0.0);
        QVERIFY(*std::ranges::max_element(ranked.timestamps) < 20.0);
        QCOMPARE(ranked.generationEnd.front(), 7);
    }

    // ── ConvergenceTracker ───────────────────────────────────────────────────
    void PositiveSettlesImmediately()
    {
        ConvergenceTracker t({0.6f}, {});
        t.Observe({0.9f});
        QVERIFY(t.Settled(0));
        QVERIFY(t.AllSettled());
        QCOMPARE(t.MaxConf(0), 0.9f);
    }

    void NegativeNeedsMinSamples()
    {
        ConvergenceTracker t({0.6f}, {}); // minSamples 7, negCeiling 0.25
        for (int i = 0; i < 6; ++i)
        {
            t.Observe({0.1f});
            QVERIFY(!t.Settled(0));
        }
        t.Observe({0.1f});
        QVERIFY(t.Settled(0)); // confident negative at 7 samples
    }

    void ModeratePositiveNeedsConfirmation()
    {
        ConvergenceTracker t({0.6f}, {});
        t.Observe({0.7f}, 4.0);
        QVERIFY(!t.Settled(0));
        QVERIFY(!t.Present(0));
        t.Observe({0.65f}, 8.0);
        QVERIFY(t.Settled(0));
        QVERIFY(t.Present(0));
        QCOMPARE(t.SupportCount(0), 2);
        QCOMPARE(t.BestTimestamp(0), 4.0);
    }

    void ResolutionPreservesAspectAndDoesNotUpscale()
    {
        int w = 0;
        int h = 0;
        InputSize(1920, 1080, 768, w, h);
        QCOMPARE(w, 768);
        QCOMPARE(h, 432);
        InputSize(640, 480, 768, w, h);
        QCOMPARE(w, 640);
        QCOMPARE(h, 480);
    }

    void FingerprintChangesWithResolutionAndMode()
    {
        std::vector<RuleEntry> entries{{"Person crouching?", "crouching", -1.0f, AnalysisMode::Auto}};
        const auto base = BuildTaggingFingerprint("model", "rev", entries, 0.6f, 31, 768);
        QVERIFY(base != BuildTaggingFingerprint("model", "rev", entries, 0.6f, 31, 1024));
        entries[0].analysisMode = AnalysisMode::HumanDetail;
        QVERIFY(base != BuildTaggingFingerprint("model", "rev", entries, 0.6f, 31, 768));
    }

    // ── Worker + store integration ───────────────────────────────────────────
    void PositiveTagIsStoredPresent()
    {
        FakeFrameSampler sampler;
        TagWorker worker(
            &sampler, ts_.get(),
            []
            {
                return std::make_unique<FakeBackend>(std::vector<float>{0.9f});
            }
        );
        worker.RunSync({MakeJob("/videos/a.mp4", {{"Does this scene contain a beach?", "beach", -1.0f}}, 0.6f, 31)});

        QCOMPARE(ts_->TagCount("/videos/a.mp4"), 1);
        TagResult r;
        QVERIFY(ts_->TagAt("/videos/a.mp4", 0, r));
        QCOMPARE(QString::fromStdString(r.tag), QStringLiteral("beach"));
        QVERIFY(r.confidence > 0.85f);
        QVERIFY(ts_->HasTag("/videos/a.mp4", "beach", 0.5f));
        QCOMPARE(sampler.readCalls, 38); // 31 thumbnails + the initial seven-frame evidence set
    }

    void HostAdapterFallsBackToSingleQuestionService()
    {
        FakeSharedInference inference;
        FakeSharedModels models;
        auto backend = CreateHostAIBackend(nullptr, &inference, &models);
        ModelSpec model;
        model.modelId = "fake";
        std::string error;
        QVERIFY(backend->LoadModel(model, error));
        std::vector<std::uint8_t> rgba(4 * 4 * 4, 255);
        std::vector<float> scores;
        QVERIFY(backend->EvaluateFrame(rgba.data(), 4, 4, {{"Is it bright?", "bright"}}, scores, error));
        QCOMPARE(static_cast<int>(scores.size()), 1);
        QCOMPARE(scores.front(), 0.8f);
        QVERIFY(inference.sawBackground);
    }

    void NegativeTagNotPresentButRecorded()
    {
        FakeFrameSampler sampler;
        TagWorker worker(
            &sampler, ts_.get(),
            []
            {
                return std::make_unique<FakeBackend>(std::vector<float>{0.1f});
            }
        );
        worker.RunSync({MakeJob("/videos/b.mp4", {{"Beach?", "beach", -1.0f}}, 0.6f, 31)});

        QCOMPARE(ts_->TagCount("/videos/b.mp4"), 0);           // not present (below threshold)
        QVERIFY(ts_->HasTag("/videos/b.mp4", "beach", 0.05f)); // but the row exists at conf 0.1
        QCOMPARE(sampler.readCalls, 38);                       // 31 thumbnails + seven full frames
    }

    void RuleRoundTripAndMostSpecificMatch()
    {
        TagRule broad;
        broad.folder = "/videos";
        broad.modelId = "m1";
        broad.entries = {{"Q?", "t1", -1.0f}};
        ts_->UpsertRule(broad);

        TagRule narrow;
        narrow.folder = "/videos/holidays";
        narrow.modelId = "m2";
        narrow.entries = {{"Q?", "t2", 0.8f, AnalysisMode::HumanDetail}};
        ts_->UpsertRule(narrow);

        TagRule got;
        QVERIFY(ts_->RuleForFile("/videos/holidays/x.mp4", got));
        QCOMPARE(QString::fromStdString(got.modelId), QStringLiteral("m2"));
        QCOMPARE(static_cast<int>(got.entries.size()), 1);
        QCOMPARE(got.entries[0].threshold, 0.8f);
        QVERIFY(got.entries[0].analysisMode == AnalysisMode::HumanDetail);

        QVERIFY(ts_->RuleForFile("/videos/other/y.mp4", got));
        QCOMPARE(QString::fromStdString(got.modelId), QStringLiteral("m1"));

        // A sibling that only shares a name prefix must not match.
        QVERIFY(!ts_->RuleForFile("/videos2/z.mp4", got));
    }

    void NeedsTaggingTracksMtimeSize()
    {
        FakeFrameSampler sampler;
        TagWorker worker(
            &sampler, ts_.get(),
            []
            {
                return std::make_unique<FakeBackend>(std::vector<float>{0.9f});
            }
        );
        worker.RunSync({MakeJob("/videos/c.mp4", {{"Q?", "beach", -1.0f}}, 0.6f, 31)});

        // Same identity (the worker recorded mtime=size=0 for a nonexistent path).
        QVERIFY(!ts_->NeedsTagging("/videos/c.mp4", 0, 0, ""));
        QVERIFY(ts_->NeedsTagging("/videos/c.mp4", 0, 0, "changed-fingerprint"));
        // Changed size ⇒ needs re-tagging.
        QVERIFY(ts_->NeedsTagging("/videos/c.mp4", 0, 123, ""));
        // Never seen ⇒ needs tagging.
        QVERIFY(ts_->NeedsTagging("/videos/never.mp4", 1, 1, ""));
    }
};

namespace
{
const ::framelift::test::Registrar<AITaggerTest> kRegisterAITaggerTest{"AITaggerTest"};
}

#include "AITaggerTests.moc"
