#include "AIService.h"
#include "FakeAIEngine.h"
#include "QtTestRunner.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtTest/QtTest>

#include <condition_variable>
#include <mutex>
#include <vector>

namespace
{
struct Results
{
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<std::uint64_t> ids;
    std::vector<AIJobState> states;
    std::vector<float> questionScores;
};

void OnComplete(const AIInferenceResult* result, void* ud)
{
    auto* results = static_cast<Results*>(ud);
    std::lock_guard lock(results->mutex);
    results->ids.push_back(result->jobId);
    results->states.push_back(result->state);
    results->changed.notify_all();
}

void OnQuestionsComplete(const AIImageQuestionResult* result, void* ud)
{
    auto* results = static_cast<Results*>(ud);
    std::lock_guard lock(results->mutex);
    results->ids.push_back(result->jobId);
    results->states.push_back(result->state);
    if (result->yesScores && result->scoreCount > 0)
    {
        results->questionScores.assign(result->yesScores, result->yesScores + result->scoreCount);
    }
    results->changed.notify_all();
}

bool WaitFor(Results& results, std::size_t count)
{
    std::unique_lock lock(results.mutex);
    return results.changed.wait_for(
        lock, std::chrono::seconds(3),
        [&]
        {
            return results.ids.size() >= count;
        }
    );
}

void EnsureModel(const char* id)
{
    const QString dir = QCoreApplication::applicationDirPath() + "/models";
    QDir().mkpath(dir);
    QFile file(dir + "/" + id + ".gguf");
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("fake");
}
} // namespace

class AIServiceTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void init()
    {
        fakeai::Reset();
        EnsureModel("test-a");
        EnsureModel("test-b");
        EnsureModel("test-c");
    }

    void cleanup()
    {
        const QString dir = QCoreApplication::applicationDirPath() + "/models/";
        QFile::remove(dir + "test-a.gguf");
        QFile::remove(dir + "test-b.gguf");
        QFile::remove(dir + "test-c.gguf");
    }

    void InteractiveJobsPassQueuedBackgroundWork()
    {
        AIService service;
        Results results;
        void* client = service.CreateClient(nullptr, OnComplete, &results);
        fakeai::BlockInference(true);

        AIInferenceRequest request;
        request.modelId = "test-a";
        request.prompt = "first";
        const std::uint64_t first = service.Submit(client, &request);
        QVERIFY(fakeai::WaitUntilRunning(std::chrono::seconds(2)));

        request.modelId = "test-b";
        request.prompt = "background";
        const std::uint64_t background = service.Submit(client, &request);
        request.modelId = "test-c";
        request.prompt = "interactive";
        request.priority = AIRequestPriority::Interactive;
        const std::uint64_t interactive = service.Submit(client, &request);

        fakeai::BlockInference(false);
        QVERIFY(WaitFor(results, 3));
        QVERIFY(results.ids == std::vector<std::uint64_t>({first, interactive, background}));
        service.DestroyClient(client);
    }

    void EvictsLeastRecentlyUsedModelAtConfiguredCount()
    {
        AIService service;
        service.SetLoadedModelLimit(2);
        Results results;
        void* client = service.CreateClient(nullptr, OnComplete, &results);
        AIInferenceRequest request;
        request.prompt = "cache";

        int completionCount = 0;
        for (const char* id : {"test-a", "test-b", "test-a", "test-c", "test-b"})
        {
            request.modelId = id;
            QVERIFY(service.Submit(client, &request) != 0);
            QVERIFY(WaitFor(results, ++completionCount));
        }
        QCOMPARE(fakeai::CreatedEngines(), 4);
        service.DestroyClient(client);
    }

    void CancelsQueuedJobAndThrottlesBackgroundDuringPlayback()
    {
        AIService service;
        service.SetPlaybackActive(true);
        Results results;
        void* client = service.CreateClient(nullptr, OnComplete, &results);
        fakeai::BlockInference(true);

        AIInferenceRequest request;
        request.modelId = "test-a";
        request.prompt = "running";
        service.Submit(client, &request);
        QVERIFY(fakeai::WaitUntilRunning(std::chrono::seconds(2)));
        request.modelId = "test-b";
        const std::uint64_t cancelled = service.Submit(client, &request);
        service.Cancel(client, cancelled);
        fakeai::BlockInference(false);

        QVERIFY(WaitFor(results, 2));
        QVERIFY(results.states.back() == AIJobState::Cancelled);
        QCOMPARE(fakeai::LastThreadLimit(), 1);
        service.DestroyClient(client);
    }

    void ScoresMultipleQuestionsInOneImageBatch()
    {
        AIService service;
        Results results;
        void* client = service.CreateScoringClient(nullptr, OnQuestionsComplete, &results);
        const char* questions[] = {"Person visible?", "Person crouching?", "Outdoors?"};
        std::vector<unsigned char> rgba(16 * 16 * 4, 255);
        AIImageQuestionRequest request;
        request.modelId = "test-a";
        request.questions = questions;
        request.questionCount = 3;
        request.image = {rgba.data(), 16, 16, 64};
        QVERIFY(service.SubmitQuestions(client, &request) != 0);
        QVERIFY(WaitFor(results, 1));
        QCOMPARE(fakeai::QuestionBatches(), 1);
        QCOMPARE(results.questionScores, std::vector<float>({0.5f, 0.6f, 0.7f}));
        service.DestroyScoringClient(client);
    }
};

namespace
{
const ::framelift::test::Registrar<AIServiceTest> kRegisterAIServiceTest{"AIServiceTest"};
}

#include "AIServiceTests.moc"
