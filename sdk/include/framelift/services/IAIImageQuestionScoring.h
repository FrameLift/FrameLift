#pragma once

#include <framelift/services/IAIInference.h>

#include <cstdint>

// Multi-question image scoring capability. Unlike IAIInference::ScoreCandidates,
// this surface makes the shared-image relationship explicit so the host can encode
// one frame once and branch multiple yes/no questions from the same visual prefix.
// SubmitQuestions copies all request strings, pointer arrays, and image pixels before
// returning; callbacks run on the host AI worker thread.
struct AIImageQuestionRequest
{
    AIRequestPriority priority = AIRequestPriority::Background;
    const char* modelId = nullptr;
    const char* systemPrompt = nullptr;
    AIImageView image{};
    const char* const* questions = nullptr;
    int questionCount = 0;
};

struct AIImageQuestionResult
{
    std::uint64_t jobId = 0;
    AIJobState state = AIJobState::Failed;
    const float* yesScores = nullptr;
    int scoreCount = 0;
    const char* error = nullptr;
};

using AIImageQuestionCompletionCallback = void (*)(const AIImageQuestionResult* result, void* userData);

class IAIImageQuestionScoring
{
public:
    static constexpr const char* InterfaceId = "framelift.IAIImageQuestionScoring";
    virtual ~IAIImageQuestionScoring() = default;

    [[nodiscard]] virtual void* CreateScoringClient(
        AIProgressCallback progress, AIImageQuestionCompletionCallback completion, void* userData
    ) noexcept = 0;
    virtual void DestroyScoringClient(void* client) noexcept = 0;
    [[nodiscard]] virtual std::uint64_t SubmitQuestions(
        void* client, const AIImageQuestionRequest* request
    ) noexcept = 0;
    virtual void CancelScoring(void* client, std::uint64_t jobId) noexcept = 0;

    // Stable for the installed model + projector files. The buf/cap contract matches
    // the other SDK string getters; pass buf=nullptr to query the required length.
    [[nodiscard]] virtual int GetModelRevision(const char* modelId, char* buf, int cap) const noexcept = 0;
};
