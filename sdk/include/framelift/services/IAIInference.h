#pragma once

#include <cstddef>
#include <cstdint>

// Shared host-owned AI inference capability. All request memory is copied by
// Submit(), so callers may release their strings, candidate arrays, and image pixels
// as soon as it returns. Callbacks run on the AI worker thread and must return
// promptly; UI work must be marshalled through IEventPump/Qt.
//
// The interface is intentionally POD-only. Adding it is a new capability and does
// not change FRAMELIFT_ABI_VERSION.
enum class AIRequestKind : std::uint8_t
{
    GenerateText,
    ScoreCandidates
};

enum class AIRequestPriority : std::uint8_t
{
    Background,
    Interactive
};

enum class AIJobState : std::uint8_t
{
    Queued,
    LoadingModel,
    Running,
    Completed,
    Cancelled,
    Failed
};

struct AIImageView
{
    const unsigned char* rgba = nullptr;
    int width = 0;
    int height = 0;
    int stride = 0;
};

struct AIInferenceRequest
{
    AIRequestKind kind = AIRequestKind::GenerateText;
    AIRequestPriority priority = AIRequestPriority::Background;
    const char* modelId = nullptr;
    const char* systemPrompt = nullptr;
    const char* prompt = nullptr;
    AIImageView image{};
    const char* const* candidates = nullptr;
    int candidateCount = 0;
    int maxTokens = 128;
    float temperature = 0.0f;
};

struct AIInferenceResult
{
    std::uint64_t jobId = 0;
    AIJobState state = AIJobState::Failed;
    const char* text = nullptr;
    const float* scores = nullptr;
    int scoreCount = 0;
    const char* error = nullptr;
};

using AIProgressCallback = void (*)(std::uint64_t jobId, AIJobState state, float progress, void* userData);
using AICompletionCallback = void (*)(const AIInferenceResult* result, void* userData);

class IAIInference
{
public:
    static constexpr const char* InterfaceId = "framelift.IAIInference";
    virtual ~IAIInference() = default;

    // A client groups jobs and callbacks belonging to one consumer. DestroyClient
    // cancels its queued/in-flight jobs and prevents callbacks after it returns.
    [[nodiscard]] virtual void* CreateClient(
        AIProgressCallback progress, AICompletionCallback completion, void* userData
    ) noexcept = 0;
    virtual void DestroyClient(void* client) noexcept = 0;

    [[nodiscard]] virtual std::uint64_t Submit(void* client, const AIInferenceRequest* request) noexcept = 0;
    virtual void Cancel(void* client, std::uint64_t jobId) noexcept = 0;
};
