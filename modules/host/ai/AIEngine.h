#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace hostai
{

struct EngineModel
{
    std::string modelPath;
    std::string projectorPath;
    int threads = 0;
    int contextSize = 4096;
};

class IAIEngine
{
public:
    virtual ~IAIEngine() = default;
    [[nodiscard]] virtual bool Load(const EngineModel& model, std::string& error) = 0;
    virtual void SetThreads(int threads) = 0;
    [[nodiscard]] virtual bool Generate(
        const unsigned char* rgba, int width, int height, const std::string& systemPrompt, const std::string& prompt,
        int maxTokens, float temperature, std::atomic<bool>& cancelled, std::string& text, std::string& error
    ) = 0;
    [[nodiscard]] virtual bool Score(
        const unsigned char* rgba, int width, int height, const std::string& systemPrompt, const std::string& prompt,
        const std::vector<std::string>& candidates, std::atomic<bool>& cancelled, std::vector<float>& scores,
        std::string& error
    ) = 0;
};

std::unique_ptr<IAIEngine> CreateLlamaEngine();

} // namespace hostai
