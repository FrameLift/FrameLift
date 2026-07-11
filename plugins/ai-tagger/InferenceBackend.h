#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// AI Tagger's narrow inference seam. Production adapts the shared host IAIInference
// service; tests inject a fake so worker/store/scheduler logic stays llama-free.
namespace aitagger
{

struct ModelSpec
{
    std::string modelPath;  // path to the model GGUF
    std::string mmprojPath; // path to the vision projector (mmproj) GGUF — required for VLMs
    std::string modelId;    // recorded on produced tags
    int nThreads = 0;       // 0 ⇒ backend default
    int nCtx = 4096;
};

struct BackendQuestion
{
    std::string question;
    std::string tag;
};

class IInferenceBackend
{
public:
    virtual ~IInferenceBackend() = default;

    // Load the model + vision projector. False on failure (err set). Loading a new
    // model implicitly unloads the previous one.
    [[nodiscard]] virtual bool LoadModel(const ModelSpec& spec, std::string& err) = 0;
    virtual void UnloadModel() = 0;

    // Live thread-count adjustment for throttling while playback is active.
    virtual void SetThreads(int nThreads) = 0;

    // Encode `rgba` (w*h*4, tightly packed) once, then answer each question. out[i] is
    // the model's yes-probability in [0,1] for questions[i]. False on failure (err set).
    [[nodiscard]] virtual bool EvaluateFrame(
        const std::uint8_t* rgba, int w, int h, const std::vector<BackendQuestion>& questions, std::vector<float>& out,
        std::string& err
    ) = 0;
};

} // namespace aitagger
