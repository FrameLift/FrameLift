#include "HostAIBackend.h"

#include <framelift/services/IAIImageQuestionScoring.h>
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
    HostAIBackend(IAIImageQuestionScoring* scoring, IAIInference* inference, IAIModelManager* models)
        : scoring_(scoring), inference_(inference), models_(models)
    {
        if (scoring_)
        {
            scoringClient_ = scoring_->CreateScoringClient(nullptr, &HostAIBackend::OnQuestionsComplete, this);
        }
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
        if (scoring_ && scoringClient_)
        {
            scoring_->DestroyScoringClient(scoringClient_);
        }
    }

    bool LoadModel(const ModelSpec& spec, std::string& error) override
    {
        if (!models_ || ((!scoring_ || !scoringClient_) && (!inference_ || !client_)))
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
        if (scoring_ && scoringClient_)
        {
            std::vector<const char*> questionPtrs;
            questionPtrs.reserve(questions.size());
            for (const auto& question : questions)
            {
                questionPtrs.push_back(question.question.c_str());
            }
            {
                std::lock_guard lock(mutex_);
                done_ = false;
                state_ = AIJobState::Failed;
                scores_.clear();
                error_.clear();
            }
            AIImageQuestionRequest request;
            request.priority = AIRequestPriority::Background;
            request.modelId = modelId_.c_str();
            request.systemPrompt =
                "Judge only visible evidence in the image. Do not infer intent or anything outside the frame.";
            request.image = {rgba, width, height, width * 4};
            request.questions = questionPtrs.data();
            request.questionCount = static_cast<int>(questionPtrs.size());
            if (scoring_->SubmitQuestions(scoringClient_, &request) == 0)
            {
                error = "shared multi-question scheduler rejected the request";
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
            if (state_ != AIJobState::Completed || scores_.size() != questions.size())
            {
                error = error_.empty() ? "shared multi-question inference failed" : error_;
                return false;
            }
            out = scores_;
            return true;
        }

        static const char* candidates[] = {"Yes", "No"};
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
            request.systemPrompt = "Judge only visible evidence in the image. Answer the question with Yes or No.";
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

    static void OnQuestionsComplete(const AIImageQuestionResult* result, void* userData)
    {
        auto* self = static_cast<HostAIBackend*>(userData);
        std::lock_guard lock(self->mutex_);
        self->state_ = result ? result->state : AIJobState::Failed;
        if (result && result->yesScores && result->scoreCount > 0)
        {
            self->scores_.assign(result->yesScores, result->yesScores + result->scoreCount);
        }
        self->error_ = result && result->error ? result->error : "";
        self->done_ = true;
        self->completed_.notify_all();
    }

    IAIImageQuestionScoring* scoring_ = nullptr;
    IAIInference* inference_ = nullptr;
    IAIModelManager* models_ = nullptr;
    void* client_ = nullptr;
    void* scoringClient_ = nullptr;
    std::string modelId_;
    std::mutex mutex_;
    std::condition_variable completed_;
    bool done_ = false;
    AIJobState state_ = AIJobState::Failed;
    std::vector<float> scores_;
    std::string error_;
};
} // namespace

std::unique_ptr<IInferenceBackend> CreateHostAIBackend(
    IAIImageQuestionScoring* scoring, IAIInference* inference, IAIModelManager* models
)
{
    return std::make_unique<HostAIBackend>(scoring, inference, models);
}
} // namespace aitagger
