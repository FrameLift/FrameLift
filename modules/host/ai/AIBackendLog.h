#pragma once

#include <mutex>
#include <string>
#include <vector>

namespace hostai
{

enum class AIBackendLogLevel
{
    Debug,
    Info,
    Warn,
    Error,
    Continue
};

struct AIBackendLogLine
{
    AIBackendLogLevel level = AIBackendLogLevel::Info;
    std::string text;
};

// FL_LOG_AI is deliberately strict: only the documented value "1" enables the
// very verbose third-party backend logs.
bool AIBackendLoggingEnabled(const char* value) noexcept;

// llama/ggml/mtmd may deliver a logical line across several callbacks using the
// CONT level. Buffer fragments per source and return only complete, non-empty lines.
class AIBackendLogBuffer
{
public:
    std::vector<AIBackendLogLine> Push(AIBackendLogLevel level, const char* text);

private:
    void FinishLine(std::vector<AIBackendLogLine>& output);

    std::mutex mutex_;
    std::string pending_;
    AIBackendLogLevel pendingLevel_ = AIBackendLogLevel::Info;
};

} // namespace hostai
