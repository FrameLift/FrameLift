#pragma once

#include "AIEngine.h"
#include "AIModelCatalog.h"

#include <framelift/services/IAIImageQuestionScoring.h>
#include <framelift/services/IAIInference.h>
#include <framelift/services/IAIModelManager.h>

#include <QtCore/QFile>
#include <QtCore/QObject>
#include <QtCore/QTimer>
#include <QtNetwork/QNetworkAccessManager>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

class QNetworkReply;

class AIService final : public QObject, public IAIInference, public IAIImageQuestionScoring, public IAIModelManager
{
public:
    explicit AIService(QObject* parent = nullptr);
    ~AIService() override;

    void* CreateClient(AIProgressCallback progress, AICompletionCallback completion, void* userData) noexcept override;
    void DestroyClient(void* client) noexcept override;
    std::uint64_t Submit(void* client, const AIInferenceRequest* request) noexcept override;
    void Cancel(void* client, std::uint64_t jobId) noexcept override;

    void* CreateScoringClient(
        AIProgressCallback progress, AIImageQuestionCompletionCallback completion, void* userData
    ) noexcept override;
    void DestroyScoringClient(void* client) noexcept override;
    std::uint64_t SubmitQuestions(void* client, const AIImageQuestionRequest* request) noexcept override;
    void CancelScoring(void* client, std::uint64_t jobId) noexcept override;
    int GetModelRevision(const char* modelId, char* buf, int cap) const noexcept override;

    void Enumerate(AIModelVisitor visitor, void* userData) const noexcept override;
    bool IsInstalled(const char* modelId) const noexcept override;
    std::uint64_t Install(const char* modelId, AIModelTransferCallback callback, void* userData) noexcept override;
    std::uint64_t Import(
        const char* modelId, const char* displayName, const char* modelPath, const char* projectorPath,
        AIModelTransferCallback callback, void* userData
    ) noexcept override;
    void CancelTransfer(std::uint64_t transferId) noexcept override;
    bool Remove(const char* modelId) noexcept override;
    int GetLoadedModelLimit() const noexcept override;
    void SetLoadedModelLimit(int limit) noexcept override;

    // Host-only playback signal. Background jobs are restricted while true and the
    // restriction is lifted ten seconds after playback becomes inactive.
    void SetPlaybackActive(bool active);

private:
    struct Client;
    struct Job;
    struct CachedModel;
    struct Transfer;

    static bool ValidModelId(const std::string& id);
    static std::string ModelsDir();
    static std::string ModelPath(const std::string& id);
    static std::string ProjectorPath(const std::string& id);
    static const hostai::CatalogModel* FindCatalog(const std::string& id);

    void WorkerMain();
    std::shared_ptr<Job> TakeJob();
    hostai::IAIEngine* LoadEngine(const std::string& modelId, std::string& error);
    void TrimCache();
    void NotifyProgress(const std::shared_ptr<Job>& job, AIJobState state, float progress);
    void Complete(
        const std::shared_ptr<Job>& job, AIJobState state, std::string text, std::vector<float> scores,
        std::string error
    );

    void StartTransferFile();
    void FinishTransferFile();
    void FailTransfer(const std::string& error);
    bool VerifySha(const QString& path, const std::string& expected) const;

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    bool stopping_ = false;
    std::deque<std::shared_ptr<Job>> interactive_;
    std::deque<std::shared_ptr<Job>> background_;
    std::unordered_map<std::uint64_t, std::weak_ptr<Job>> jobs_;
    std::vector<std::shared_ptr<Client>> clients_;
    std::vector<CachedModel> cache_;
    std::thread worker_;
    std::uint64_t useTick_ = 0;
    std::atomic<std::uint64_t> nextId_{1};
    std::atomic<int> modelLimit_{2};
    std::atomic<bool> playbackThrottled_{false};
    QTimer playbackRestore_;

    QNetworkAccessManager network_;
    std::unique_ptr<Transfer> transfer_;
};
