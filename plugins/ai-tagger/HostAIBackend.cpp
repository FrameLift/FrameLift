#include "HostAIBackend.h"

#include <framelift/services/IAIInference.h>
#include <framelift/services/IAIModelManager.h>

#include <condition_variable>
#include <mutex>

namespace aitagger
{
namespace
{
class HostAIBackend final : public IInferenceBackend
{
public:
    HostAIBackend(IAIInference* inference, IAIModelManager* models) : inference_(inference), models_(models)
    {
        if (inference_)
        {
            client_ = inference_->CreateClient(nullptr, &HostAIBackend::OnComplete, this);
        }
    }

    ~HostAIBackend() override
    {
        if (inference_ && client_)
        {
            inference_->DestroyClient(client_);
        }
    }

    bool LoadModel(const ModelSpec& spec, std::string& error) override
    {
        if (!inference_ || !models_ || !client_)
        {
            error = "shared AI host service is unavailable";
            return false;
        }
        if (!models_->IsInstalled(spec.modelId.c_str()))
        {
            error = "model is not installed: " + spec.modelId;
            return false;
        }
        modelId_ = spec.modelId;
        return true;
    }

    void UnloadModel() override
    {
        modelId_.clear();
    }

    void SetThreads(int) override
    {
        // Scheduling and playback-aware thread limits are host policy.
    }

    bool EvaluateFrame(
        const std::uint8_t* rgba, int width, int height, const std::vector<BackendQuestion>& questions,
        std::vector<float>& out, std::string& error
    ) override
    {
        out.assign(questions.size(), 0.0f);
        if (modelId_.empty())
        {
            error = "model is not loaded";
            return false;
        }
        static const char* candidates[] = {" yes", " no"};
        for (std::size_t i = 0; i < questions.size(); ++i)
        {
            {
                std::lock_guard lock(mutex_);
                done_ = false;
                state_ = AIJobState::Failed;
                scores_.clear();
                error_.clear();
            }
            AIInferenceRequest request;
            request.kind = AIRequestKind::ScoreCandidates;
            request.priority = AIRequestPriority::Background;
            request.modelId = modelId_.c_str();
            request.systemPrompt = "Answer the question about the image with yes or no.";
            request.prompt = questions[i].question.c_str();
            request.image = {rgba, width, height, width * 4};
            request.candidates = candidates;
            request.candidateCount = 2;
            const std::uint64_t job = inference_->Submit(client_, &request);
            if (job == 0)
            {
                error = "shared AI scheduler rejected the request";
                return false;
            }
            std::unique_lock lock(mutex_);
            completed_.wait(
                lock,
                [this]
                {
                    return done_;
                }
            );
            if (state_ != AIJobState::Completed || scores_.empty())
            {
                error = error_.empty() ? "shared AI inference failed" : error_;
                return false;
            }
            out[i] = scores_.front();
        }
        return true;
    }

private:
    static void OnComplete(const AIInferenceResult* result, void* userData)
    {
        auto* self = static_cast<HostAIBackend*>(userData);
        std::lock_guard lock(self->mutex_);
        self->state_ = result ? result->state : AIJobState::Failed;
        if (result && result->scores && result->scoreCount > 0)
        {
            self->scores_.assign(result->scores, result->scores + result->scoreCount);
        }
        self->error_ = result && result->error ? result->error : "";
        self->done_ = true;
        self->completed_.notify_all();
    }

    IAIInference* inference_ = nullptr;
    IAIModelManager* models_ = nullptr;
    void* client_ = nullptr;
    std::string modelId_;
    std::mutex mutex_;
    std::condition_variable completed_;
    bool done_ = false;
    AIJobState state_ = AIJobState::Failed;
    std::vector<float> scores_;
    std::string error_;
};
} // namespace

std::unique_ptr<IInferenceBackend> CreateHostAIBackend(IAIInference* inference, IAIModelManager* models)
{
    return std::make_unique<HostAIBackend>(inference, models);
}
} // namespace aitagger
