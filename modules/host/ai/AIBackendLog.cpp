#include "AIBackendLog.h"

#include <cstring>
#include <utility>

namespace hostai
{

bool AIBackendLoggingEnabled(const char* value) noexcept
{
    return value && std::strcmp(value, "1") == 0;
}

std::vector<AIBackendLogLine> AIBackendLogBuffer::Push(AIBackendLogLevel level, const char* text)
{
    std::lock_guard lock(mutex_);
    std::vector<AIBackendLogLine> output;

    // A new non-continuation record starts a new logical line. Do not lose a
    // previous unterminated fragment if an upstream component omitted its newline.
    if (level != AIBackendLogLevel::Continue)
    {
        if (!pending_.empty())
        {
            FinishLine(output);
        }
        pendingLevel_ = level;
    }

    if (!text || text[0] == '\0')
    {
        return output;
    }
    pending_ += text;

    std::size_t newline = pending_.find('\n');
    while (newline != std::string::npos)
    {
        std::string remainder = pending_.substr(newline + 1);
        pending_.resize(newline);
        FinishLine(output);
        pending_ = std::move(remainder);
        newline = pending_.find('\n');
    }
    return output;
}

void AIBackendLogBuffer::FinishLine(std::vector<AIBackendLogLine>& output)
{
    while (!pending_.empty() && pending_.back() == '\r')
    {
        pending_.pop_back();
    }
    if (!pending_.empty())
    {
        output.push_back({pendingLevel_, std::move(pending_)});
    }
    pending_.clear();
}

} // namespace hostai
