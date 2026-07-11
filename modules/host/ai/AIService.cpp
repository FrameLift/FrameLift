#include "AIService.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QSettings>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace
{
long long FileSignature(const std::string& path)
{
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error)
    {
        return 0;
    }
    const auto modified = std::filesystem::last_write_time(path, error);
    return error ? static_cast<long long>(size)
                 : static_cast<long long>(size) ^ static_cast<long long>(modified.time_since_epoch().count());
}
} // namespace

struct AIService::Client
{
    std::mutex callbackMutex;
    bool alive = true;
    AIProgressCallback progress = nullptr;
    AICompletionCallback completion = nullptr;
    AIImageQuestionCompletionCallback questionCompletion = nullptr;
    void* userData = nullptr;
};

struct AIService::Job
{
    std::uint64_t id = 0;
    std::shared_ptr<Client> client;
    AIRequestKind kind = AIRequestKind::GenerateText;
    AIRequestPriority priority = AIRequestPriority::Background;
    std::string modelId;
    std::string systemPrompt;
    std::string prompt;
    std::vector<std::string> candidates;
    std::vector<std::string> questions;
    std::vector<unsigned char> rgba;
    int width = 0;
    int height = 0;
    int maxTokens = 128;
    float temperature = 0.0f;
    std::atomic<bool> cancelled{false};
    bool multiQuestion = false;
};

struct AIService::CachedModel
{
    std::string id;
    std::unique_ptr<hostai::IAIEngine> engine;
    std::uint64_t lastUse = 0;
    long long signature = 0;
};

struct AIService::Transfer
{
    std::uint64_t id = 0;
    const hostai::CatalogModel* catalog = nullptr;
    std::string modelId;
    AIModelTransferCallback callback = nullptr;
    void* userData = nullptr;
    int fileIndex = 0;
    QNetworkReply* reply = nullptr;
    std::unique_ptr<QFile> file;
    QString target;
    qint64 received = 0;
    qint64 total = 0;
};

AIService::AIService(QObject* parent) : QObject(parent)
{
    QDir().mkpath(QString::fromStdString(ModelsDir()));
    QSettings settings(QString::fromStdString(ModelsDir() + "/models.ini"), QSettings::IniFormat);
    modelLimit_.store(std::clamp(settings.value("cache/loadedModels", 2).toInt(), 1, 8));
    playbackRestore_.setSingleShot(true);
    playbackRestore_.setInterval(10000);
    connect(
        &playbackRestore_, &QTimer::timeout, this,
        [this]
        {
            playbackThrottled_.store(false);
        }
    );
    worker_ = std::thread(
        [this]
        {
            WorkerMain();
        }
    );
}

AIService::~AIService()
{
    if (transfer_)
    {
        CancelTransfer(transfer_->id);
    }
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        for (auto& [id, weak] : jobs_)
        {
            if (auto job = weak.lock())
            {
                job->cancelled.store(true);
            }
        }
    }
    wake_.notify_all();
    if (worker_.joinable())
    {
        worker_.join();
    }
    for (const auto& client : clients_)
    {
        std::lock_guard callbackLock(client->callbackMutex);
        client->alive = false;
    }
}

void* AIService::CreateClient(AIProgressCallback progress, AICompletionCallback completion, void* userData) noexcept
{
    try
    {
        auto client = std::make_shared<Client>();
        client->progress = progress;
        client->completion = completion;
        client->userData = userData;
        void* handle = client.get();
        std::lock_guard lock(mutex_);
        clients_.push_back(std::move(client));
        return handle;
    }
    catch (...)
    {
        return nullptr;
    }
}

void* AIService::CreateScoringClient(
    AIProgressCallback progress, AIImageQuestionCompletionCallback completion, void* userData
) noexcept
{
    try
    {
        auto client = std::make_shared<Client>();
        client->progress = progress;
        client->questionCompletion = completion;
        client->userData = userData;
        void* handle = client.get();
        std::lock_guard lock(mutex_);
        clients_.push_back(std::move(client));
        return handle;
    }
    catch (...)
    {
        return nullptr;
    }
}

void AIService::DestroyClient(void* handle) noexcept
{
    if (!handle)
    {
        return;
    }
    std::shared_ptr<Client> client;
    {
        std::lock_guard lock(mutex_);
        const auto it = std::ranges::find_if(
            clients_,
            [handle](const auto& item)
            {
                return item.get() == handle;
            }
        );
        if (it == clients_.end())
        {
            return;
        }
        client = *it;
        for (auto& [id, weak] : jobs_)
        {
            if (auto job = weak.lock(); job && job->client == client)
            {
                job->cancelled.store(true);
            }
        }
        clients_.erase(it);
    }
    std::lock_guard callbackLock(client->callbackMutex);
    client->alive = false;
    wake_.notify_all();
}

void AIService::DestroyScoringClient(void* handle) noexcept
{
    DestroyClient(handle);
}

std::uint64_t AIService::Submit(void* handle, const AIInferenceRequest* request) noexcept
{
    if (!handle || !request || !request->modelId || request->modelId[0] == '\0')
    {
        return 0;
    }
    try
    {
        std::shared_ptr<Client> client;
        auto job = std::make_shared<Job>();
        {
            std::lock_guard lock(mutex_);
            const auto it = std::ranges::find_if(
                clients_,
                [handle](const auto& item)
                {
                    return item.get() == handle;
                }
            );
            if (it == clients_.end())
            {
                return 0;
            }
            client = *it;
        }
        job->id = nextId_.fetch_add(1);
        job->client = std::move(client);
        job->kind = request->kind;
        job->priority = request->priority;
        job->modelId = request->modelId;
        job->systemPrompt = request->systemPrompt ? request->systemPrompt : "";
        job->prompt = request->prompt ? request->prompt : "";
        job->maxTokens = request->maxTokens;
        job->temperature = request->temperature;
        if (request->candidates && request->candidateCount > 0)
        {
            for (int i = 0; i < request->candidateCount; ++i)
            {
                job->candidates.emplace_back(request->candidates[i] ? request->candidates[i] : "");
            }
        }
        if (request->image.rgba && request->image.width > 0 && request->image.height > 0)
        {
            job->width = request->image.width;
            job->height = request->image.height;
            const int stride = request->image.stride > 0 ? request->image.stride : job->width * 4;
            job->rgba.resize(static_cast<std::size_t>(job->width) * job->height * 4);
            for (int y = 0; y < job->height; ++y)
            {
                std::memcpy(
                    job->rgba.data() + static_cast<std::size_t>(y) * job->width * 4,
                    request->image.rgba + static_cast<std::size_t>(y) * stride, static_cast<std::size_t>(job->width) * 4
                );
            }
        }
        {
            std::lock_guard lock(mutex_);
            jobs_[job->id] = job;
            (job->priority == AIRequestPriority::Interactive ? interactive_ : background_).push_back(job);
        }
        NotifyProgress(job, AIJobState::Queued, 0.0f);
        wake_.notify_one();
        return job->id;
    }
    catch (...)
    {
        return 0;
    }
}

std::uint64_t AIService::SubmitQuestions(void* handle, const AIImageQuestionRequest* request) noexcept
{
    if (!handle || !request || !request->modelId || request->modelId[0] == '\0' || !request->questions ||
        request->questionCount <= 0)
    {
        return 0;
    }
    try
    {
        std::shared_ptr<Client> client;
        auto job = std::make_shared<Job>();
        {
            std::lock_guard lock(mutex_);
            const auto it = std::ranges::find_if(
                clients_,
                [handle](const auto& item)
                {
                    return item.get() == handle;
                }
            );
            if (it == clients_.end() || !(*it)->questionCompletion)
            {
                return 0;
            }
            client = *it;
        }
        job->id = nextId_.fetch_add(1);
        job->client = std::move(client);
        job->priority = request->priority;
        job->modelId = request->modelId;
        job->systemPrompt = request->systemPrompt ? request->systemPrompt : "";
        job->multiQuestion = true;
        for (int i = 0; i < request->questionCount; ++i)
        {
            job->questions.emplace_back(request->questions[i] ? request->questions[i] : "");
        }
        if (request->image.rgba && request->image.width > 0 && request->image.height > 0)
        {
            job->width = request->image.width;
            job->height = request->image.height;
            const int stride = request->image.stride > 0 ? request->image.stride : job->width * 4;
            job->rgba.resize(static_cast<std::size_t>(job->width) * job->height * 4);
            for (int y = 0; y < job->height; ++y)
            {
                std::memcpy(
                    job->rgba.data() + static_cast<std::size_t>(y) * job->width * 4,
                    request->image.rgba + static_cast<std::size_t>(y) * stride, static_cast<std::size_t>(job->width) * 4
                );
            }
        }
        {
            std::lock_guard lock(mutex_);
            jobs_[job->id] = job;
            (job->priority == AIRequestPriority::Interactive ? interactive_ : background_).push_back(job);
        }
        NotifyProgress(job, AIJobState::Queued, 0.0f);
        wake_.notify_one();
        return job->id;
    }
    catch (...)
    {
        return 0;
    }
}

void AIService::Cancel(void* handle, std::uint64_t jobId) noexcept
{
    std::lock_guard lock(mutex_);
    const auto it = jobs_.find(jobId);
    if (it == jobs_.end())
    {
        return;
    }
    if (auto job = it->second.lock(); job && job->client.get() == handle)
    {
        job->cancelled.store(true);
        wake_.notify_all();
    }
}

void AIService::CancelScoring(void* handle, std::uint64_t jobId) noexcept
{
    Cancel(handle, jobId);
}

std::shared_ptr<AIService::Job> AIService::TakeJob()
{
    std::unique_lock lock(mutex_);
    wake_.wait(
        lock,
        [this]
        {
            return stopping_ || !interactive_.empty() || !background_.empty();
        }
    );
    if (stopping_)
    {
        return {};
    }
    auto& queue = !interactive_.empty() ? interactive_ : background_;
    auto job = queue.front();
    queue.pop_front();
    return job;
}

void AIService::WorkerMain()
{
    while (auto job = TakeJob())
    {
        if (job->cancelled.load())
        {
            Complete(job, AIJobState::Cancelled, {}, {}, {});
            continue;
        }
        NotifyProgress(job, AIJobState::LoadingModel, 0.1f);
        std::string error;
        hostai::IAIEngine* engine = LoadEngine(job->modelId, error);
        if (!engine)
        {
            Complete(job, AIJobState::Failed, {}, {}, std::move(error));
            continue;
        }
        const bool throttled = job->priority == AIRequestPriority::Background && playbackThrottled_.load();
        engine->SetThreads(throttled ? 1 : static_cast<int>(std::max(1u, std::thread::hardware_concurrency())));
        if (throttled)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(120));
        }
        NotifyProgress(job, AIJobState::Running, 0.25f);
        std::string text;
        std::vector<float> scores;
        bool ok = false;
        if (job->multiQuestion)
        {
            ok = engine->ScoreQuestions(
                job->rgba.empty() ? nullptr : job->rgba.data(), job->width, job->height, job->systemPrompt,
                job->questions, job->cancelled, scores, error
            );
        }
        else if (job->kind == AIRequestKind::GenerateText)
        {
            ok = engine->Generate(
                job->rgba.empty() ? nullptr : job->rgba.data(), job->width, job->height, job->systemPrompt, job->prompt,
                job->maxTokens, job->temperature, job->cancelled, text, error
            );
        }
        else
        {
            ok = engine->Score(
                job->rgba.empty() ? nullptr : job->rgba.data(), job->width, job->height, job->systemPrompt, job->prompt,
                job->candidates, job->cancelled, scores, error
            );
        }
        Complete(
            job, job->cancelled.load() ? AIJobState::Cancelled : (ok ? AIJobState::Completed : AIJobState::Failed),
            std::move(text), std::move(scores), std::move(error)
        );
    }
}

hostai::IAIEngine* AIService::LoadEngine(const std::string& modelId, std::string& error)
{
    const std::string modelPath = ModelPath(modelId);
    if (!std::filesystem::is_regular_file(modelPath))
    {
        std::erase_if(
            cache_,
            [&](const CachedModel& cached)
            {
                return cached.id == modelId;
            }
        );
        error = "model is not installed: " + modelId;
        return nullptr;
    }
    const long long signature = FileSignature(modelPath) ^ FileSignature(ProjectorPath(modelId));
    for (auto it = cache_.begin(); it != cache_.end(); ++it)
    {
        if (it->id == modelId && it->signature == signature)
        {
            it->lastUse = ++useTick_;
            return it->engine.get();
        }
        if (it->id == modelId)
        {
            cache_.erase(it);
            break;
        }
    }
    auto engine = hostai::CreateLlamaEngine();
    hostai::EngineModel spec;
    spec.modelPath = modelPath;
    if (std::filesystem::is_regular_file(ProjectorPath(modelId)))
    {
        spec.projectorPath = ProjectorPath(modelId);
    }
    if (!engine->Load(spec, error))
    {
        return nullptr;
    }
    cache_.push_back({modelId, std::move(engine), ++useTick_, signature});
    TrimCache();
    return cache_.back().engine.get();
}

void AIService::TrimCache()
{
    while (static_cast<int>(cache_.size()) > modelLimit_.load())
    {
        const auto it = std::ranges::min_element(cache_, {}, &CachedModel::lastUse);
        cache_.erase(it);
    }
}

void AIService::NotifyProgress(const std::shared_ptr<Job>& job, AIJobState state, float progress)
{
    std::lock_guard callbackLock(job->client->callbackMutex);
    if (job->client->alive && job->client->progress)
    {
        job->client->progress(job->id, state, progress, job->client->userData);
    }
}

void AIService::Complete(
    const std::shared_ptr<Job>& job, AIJobState state, std::string text, std::vector<float> scores, std::string error
)
{
    {
        std::lock_guard callbackLock(job->client->callbackMutex);
        if (job->client->alive && job->multiQuestion && job->client->questionCompletion)
        {
            const AIImageQuestionResult result{
                job->id, state, scores.data(), static_cast<int>(scores.size()), error.c_str()
            };
            job->client->questionCompletion(&result, job->client->userData);
        }
        else if (job->client->alive && job->client->completion)
        {
            const AIInferenceResult result{
                job->id, state, text.c_str(), scores.data(), static_cast<int>(scores.size()), error.c_str()
            };
            job->client->completion(&result, job->client->userData);
        }
    }
    std::lock_guard lock(mutex_);
    jobs_.erase(job->id);
}

int AIService::GetModelRevision(const char* modelId, char* buf, int cap) const noexcept
{
    if (!modelId || !ValidModelId(modelId))
    {
        if (buf && cap > 0)
        {
            buf[0] = '\0';
        }
        return 0;
    }
    const auto modelSignature = static_cast<unsigned long long>(FileSignature(ModelPath(modelId)));
    const auto projectorSignature = static_cast<unsigned long long>(FileSignature(ProjectorPath(modelId)));
    const auto signature = modelSignature ^ (projectorSignature << 1);
    std::ostringstream stream;
    stream << std::hex << std::setw(16) << std::setfill('0') << signature;
    const std::string revision = stream.str();
    if (buf && cap > 0)
    {
        const int n = std::min(static_cast<int>(revision.size()), cap - 1);
        std::memcpy(buf, revision.data(), static_cast<std::size_t>(n));
        buf[n] = '\0';
    }
    return static_cast<int>(revision.size());
}

bool AIService::ValidModelId(const std::string& id)
{
    return !id.empty() && std::ranges::all_of(
                              id,
                              [](unsigned char c)
                              {
                                  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                                         c == '-' || c == '_';
                              }
                          );
}

std::string AIService::ModelsDir()
{
    return (std::filesystem::path(QCoreApplication::applicationDirPath().toStdString()) / "models").string();
}

std::string AIService::ModelPath(const std::string& id)
{
    return (std::filesystem::path(ModelsDir()) / (id + ".gguf")).string();
}

std::string AIService::ProjectorPath(const std::string& id)
{
    return (std::filesystem::path(ModelsDir()) / (id + ".mmproj.gguf")).string();
}

const hostai::CatalogModel* AIService::FindCatalog(const std::string& id)
{
    const auto& catalog = hostai::BuiltinModelCatalog();
    const auto it = std::ranges::find(catalog, id, &hostai::CatalogModel::id);
    return it == catalog.end() ? nullptr : &*it;
}

void AIService::Enumerate(AIModelVisitor visitor, void* userData) const noexcept
{
    if (!visitor)
    {
        return;
    }
    try
    {
        std::vector<std::string> known;
        for (const auto& model : hostai::BuiltinModelCatalog())
        {
            known.push_back(model.id);
            const AIModelInfo info{
                model.id.c_str(),
                model.name.c_str(),
                model.quant.c_str(),
                AIModelSource::Catalog,
                model.modelSize + model.projectorSize,
                std::filesystem::is_regular_file(ModelPath(model.id)) &&
                    (model.projectorUrl.empty() || std::filesystem::is_regular_file(ProjectorPath(model.id))),
                model.recommended,
                true,
                !model.projectorUrl.empty()
            };
            visitor(&info, userData);
        }
        QSettings registry(QString::fromStdString(ModelsDir() + "/models.ini"), QSettings::IniFormat);
        registry.beginGroup("imports");
        for (const QString& qid : registry.childGroups())
        {
            const std::string id = qid.toStdString();
            if (std::ranges::find(known, id) != known.end())
            {
                continue;
            }
            registry.beginGroup(qid);
            const std::string name = registry.value("name", qid).toString().toStdString();
            registry.endGroup();
            const bool vision = std::filesystem::is_regular_file(ProjectorPath(id));
            const AIModelInfo info{
                id.c_str(),
                name.c_str(),
                "local",
                AIModelSource::Imported,
                static_cast<long long>(QFileInfo(QString::fromStdString(ModelPath(id))).size()),
                std::filesystem::is_regular_file(ModelPath(id)),
                false,
                true,
                vision
            };
            visitor(&info, userData);
        }
        registry.endGroup();
    }
    catch (...)
    {
    }
}

bool AIService::IsInstalled(const char* modelId) const noexcept
{
    if (!modelId || !ValidModelId(modelId) || !std::filesystem::is_regular_file(ModelPath(modelId)))
    {
        return false;
    }
    const auto* catalog = FindCatalog(modelId);
    return !catalog || catalog->projectorUrl.empty() || std::filesystem::is_regular_file(ProjectorPath(modelId));
}

std::uint64_t AIService::Install(const char* modelId, AIModelTransferCallback callback, void* userData) noexcept
{
    if (transfer_ || !modelId)
    {
        return 0;
    }
    const auto* model = FindCatalog(modelId);
    if (!model)
    {
        return 0;
    }
    transfer_ = std::make_unique<Transfer>();
    transfer_->id = nextId_.fetch_add(1);
    transfer_->catalog = model;
    transfer_->modelId = modelId;
    transfer_->callback = callback;
    transfer_->userData = userData;
    const std::uint64_t id = transfer_->id;
    QTimer::singleShot(
        0, this,
        [this]
        {
            StartTransferFile();
        }
    );
    return id;
}

void AIService::StartTransferFile()
{
    if (!transfer_)
    {
        return;
    }
    const bool projector = transfer_->fileIndex == 1;
    const std::string url = projector ? transfer_->catalog->projectorUrl : transfer_->catalog->modelUrl;
    if (url.empty())
    {
        FinishTransferFile();
        return;
    }
    transfer_->target =
        QString::fromStdString(projector ? ProjectorPath(transfer_->modelId) : ModelPath(transfer_->modelId));
    if (QFile::exists(transfer_->target))
    {
        FinishTransferFile();
        return;
    }
    const QString part = transfer_->target + ".part";
    transfer_->file = std::make_unique<QFile>(part);
    const qint64 resume = QFileInfo(part).size();
    if (!transfer_->file->open(QIODevice::WriteOnly | QIODevice::Append))
    {
        FailTransfer("cannot open model download temporary file");
        return;
    }
    transfer_->received = resume;
    transfer_->total = projector ? transfer_->catalog->projectorSize : transfer_->catalog->modelSize;
    QNetworkRequest request(QUrl(QString::fromStdString(url)));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    if (resume > 0)
    {
        request.setRawHeader("Range", "bytes=" + QByteArray::number(resume) + "-");
    }
    transfer_->reply = network_.get(request);
    connect(
        transfer_->reply, &QNetworkReply::readyRead, this,
        [this]
        {
            if (!transfer_ || !transfer_->reply || !transfer_->file)
            {
                return;
            }
            const QByteArray data = transfer_->reply->readAll();
            transfer_->file->write(data);
            transfer_->received += data.size();
            const float local =
                transfer_->total > 0 ? static_cast<float>(transfer_->received) / transfer_->total : 0.0f;
            if (transfer_->callback)
            {
                transfer_->callback(
                    transfer_->id, transfer_->modelId.c_str(),
                    (transfer_->fileIndex + std::clamp(local, 0.0f, 1.0f)) / 2.0f, false, nullptr, transfer_->userData
                );
            }
        }
    );
    connect(
        transfer_->reply, &QNetworkReply::finished, this,
        [this]
        {
            if (!transfer_ || !transfer_->reply)
            {
                return;
            }
            const bool ok = transfer_->reply->error() == QNetworkReply::NoError;
            transfer_->reply->deleteLater();
            transfer_->reply = nullptr;
            if (transfer_->file)
            {
                transfer_->file->flush();
                transfer_->file->close();
                transfer_->file.reset();
            }
            if (!ok)
            {
                FailTransfer("network error while downloading model");
                return;
            }
            const QString part = transfer_->target + ".part";
            const std::string expected =
                transfer_->fileIndex == 0 ? transfer_->catalog->modelSha256 : transfer_->catalog->projectorSha256;
            if (!expected.empty() && !VerifySha(part, expected))
            {
                QFile::remove(part);
                FailTransfer("model checksum mismatch");
                return;
            }
            if (!QFile::rename(part, transfer_->target))
            {
                FailTransfer("cannot finalize model download");
                return;
            }
            FinishTransferFile();
        }
    );
}

void AIService::FinishTransferFile()
{
    if (!transfer_)
    {
        return;
    }
    if (transfer_->fileIndex == 0 && !transfer_->catalog->projectorUrl.empty())
    {
        transfer_->fileIndex = 1;
        StartTransferFile();
        return;
    }
    auto done = std::move(transfer_);
    if (done->callback)
    {
        done->callback(done->id, done->modelId.c_str(), 1.0f, true, nullptr, done->userData);
    }
}

void AIService::FailTransfer(const std::string& error)
{
    auto failed = std::move(transfer_);
    if (failed && failed->callback)
    {
        failed->callback(failed->id, failed->modelId.c_str(), 0.0f, true, error.c_str(), failed->userData);
    }
}

bool AIService::VerifySha(const QString& path, const std::string& expected) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return false;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return hash.addData(&file) &&
           hash.result().toHex().compare(QByteArray::fromStdString(expected), Qt::CaseInsensitive) == 0;
}

std::uint64_t AIService::Import(
    const char* modelId, const char* displayName, const char* modelPath, const char* projectorPath,
    AIModelTransferCallback callback, void* userData
) noexcept
{
    if (!modelId || !modelPath || !ValidModelId(modelId) || !QFile::exists(QString::fromUtf8(modelPath)))
    {
        return 0;
    }
    const std::uint64_t id = nextId_.fetch_add(1);
    const QString target = QString::fromStdString(ModelPath(modelId));
    const QString targetPart = target + ".import.part";
    QFile::remove(targetPart);
    bool ok = QFile::copy(QString::fromUtf8(modelPath), targetPart);
    if (ok)
    {
        QFile::remove(target);
        ok = QFile::rename(targetPart, target);
    }
    if (ok && projectorPath && projectorPath[0])
    {
        const QString projectorTarget = QString::fromStdString(ProjectorPath(modelId));
        const QString projectorPart = projectorTarget + ".import.part";
        QFile::remove(projectorPart);
        ok = QFile::copy(QString::fromUtf8(projectorPath), projectorPart);
        if (ok)
        {
            QFile::remove(projectorTarget);
            ok = QFile::rename(projectorPart, projectorTarget);
        }
    }
    if (ok)
    {
        QSettings registry(QString::fromStdString(ModelsDir() + "/models.ini"), QSettings::IniFormat);
        registry.setValue(
            QString("imports/%1/name").arg(modelId), QString::fromUtf8(displayName ? displayName : modelId)
        );
    }
    if (callback)
    {
        callback(id, modelId, ok ? 1.0f : 0.0f, true, ok ? nullptr : "failed to copy imported model", userData);
    }
    return id;
}

void AIService::CancelTransfer(std::uint64_t transferId) noexcept
{
    if (!transfer_ || transfer_->id != transferId)
    {
        return;
    }
    if (transfer_->reply)
    {
        transfer_->reply->disconnect(this);
        transfer_->reply->abort();
        transfer_->reply->deleteLater();
    }
    if (transfer_->file)
    {
        transfer_->file->close();
    }
    auto cancelled = std::move(transfer_);
    if (cancelled->callback)
    {
        cancelled->callback(cancelled->id, cancelled->modelId.c_str(), 0.0f, true, "cancelled", cancelled->userData);
    }
}

bool AIService::Remove(const char* modelId) noexcept
{
    if (!modelId || !ValidModelId(modelId))
    {
        return false;
    }
    bool removed = QFile::remove(QString::fromStdString(ModelPath(modelId)));
    removed = QFile::remove(QString::fromStdString(ProjectorPath(modelId))) || removed;
    QSettings registry(QString::fromStdString(ModelsDir() + "/models.ini"), QSettings::IniFormat);
    registry.remove(QString("imports/%1").arg(modelId));
    return removed;
}

int AIService::GetLoadedModelLimit() const noexcept
{
    return modelLimit_.load();
}

void AIService::SetLoadedModelLimit(int limit) noexcept
{
    limit = std::clamp(limit, 1, 8);
    modelLimit_.store(limit);
    QSettings settings(QString::fromStdString(ModelsDir() + "/models.ini"), QSettings::IniFormat);
    settings.setValue("cache/loadedModels", limit);
}

void AIService::SetPlaybackActive(bool active)
{
    if (active)
    {
        playbackRestore_.stop();
        playbackThrottled_.store(true);
    }
    else
    {
        playbackRestore_.start();
    }
}
