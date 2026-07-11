#include "FakeAIEngine.h"

#include "AIEngine.h"

#include <condition_variable>
#include <mutex>

namespace
{
std::mutex g_mutex;
std::condition_variable g_changed;
bool g_block = false;
bool g_running = false;
int g_created = 0;
int g_threads = 0;
int g_questionBatches = 0;

class FakeEngine final : public hostai::IAIEngine
{
public:
    bool Load(const hostai::EngineModel&, std::string&) override
    {
        return true;
    }

    void SetThreads(int threads) override
    {
        std::lock_guard lock(g_mutex);
        g_threads = threads;
    }

    bool Generate(
        const unsigned char*, int, int, const std::string&, const std::string& prompt, int, float,
        std::atomic<bool>& cancelled, std::string& text, std::string&
    ) override
    {
        std::unique_lock lock(g_mutex);
        g_running = true;
        g_changed.notify_all();
        g_changed.wait(
            lock,
            [&]
            {
                return !g_block || cancelled.load();
            }
        );
        text = prompt;
        return !cancelled.load();
    }

    bool Score(
        const unsigned char*, int, int, const std::string&, const std::string&,
        const std::vector<std::string>& candidates, std::atomic<bool>& cancelled, std::vector<float>& scores,
        std::string&
    ) override
    {
        scores.assign(candidates.size(), candidates.empty() ? 0.0f : 1.0f / candidates.size());
        return !cancelled.load();
    }

    bool ScoreQuestions(
        const unsigned char*, int, int, const std::string&, const std::vector<std::string>& questions,
        std::atomic<bool>& cancelled, std::vector<float>& scores, std::string&
    ) override
    {
        std::lock_guard lock(g_mutex);
        ++g_questionBatches;
        scores.resize(questions.size());
        for (std::size_t i = 0; i < questions.size(); ++i)
        {
            scores[i] = 0.5f + static_cast<float>(i) * 0.1f;
        }
        return !cancelled.load();
    }
};
} // namespace

namespace hostai
{
std::unique_ptr<IAIEngine> CreateLlamaEngine()
{
    std::lock_guard lock(g_mutex);
    ++g_created;
    return std::make_unique<FakeEngine>();
}
} // namespace hostai

namespace fakeai
{
void Reset()
{
    std::lock_guard lock(g_mutex);
    g_block = false;
    g_running = false;
    g_created = 0;
    g_threads = 0;
    g_questionBatches = 0;
}

void BlockInference(bool block)
{
    std::lock_guard lock(g_mutex);
    g_block = block;
    if (!block)
    {
        g_changed.notify_all();
    }
}

bool WaitUntilRunning(std::chrono::milliseconds timeout)
{
    std::unique_lock lock(g_mutex);
    return g_changed.wait_for(
        lock, timeout,
        []
        {
            return g_running;
        }
    );
}

int CreatedEngines()
{
    std::lock_guard lock(g_mutex);
    return g_created;
}

int LastThreadLimit()
{
    std::lock_guard lock(g_mutex);
    return g_threads;
}

int QuestionBatches()
{
    std::lock_guard lock(g_mutex);
    return g_questionBatches;
}
} // namespace fakeai
