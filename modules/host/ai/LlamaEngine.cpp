#include "AIBackendLog.h"
#include "AIEngine.h"

#include <framelift/Log.h>

#include <llama.h>
#include <mtmd-helper.h>
#include <mtmd.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace hostai
{
namespace
{
std::once_flag g_backendInit;
bool g_logAI = false;

struct BackendLogger
{
    const char* source;
    AIBackendLogBuffer buffer;
};

BackendLogger g_llamaLogger{"llama"};
BackendLogger g_ggmlLogger{"ggml"};
BackendLogger g_mtmdLogger{"mtmd"};

AIBackendLogLevel MapLogLevel(ggml_log_level level)
{
    switch (level)
    {
    case GGML_LOG_LEVEL_DEBUG:
    case GGML_LOG_LEVEL_NONE:
        return AIBackendLogLevel::Debug;
    case GGML_LOG_LEVEL_INFO:
        return AIBackendLogLevel::Info;
    case GGML_LOG_LEVEL_WARN:
        return AIBackendLogLevel::Warn;
    case GGML_LOG_LEVEL_ERROR:
        return AIBackendLogLevel::Error;
    case GGML_LOG_LEVEL_CONT:
        return AIBackendLogLevel::Continue;
    }
    return AIBackendLogLevel::Debug;
}

void EmitBackendLine(const BackendLogger& logger, const AIBackendLogLine& line)
{
    switch (line.level)
    {
    case AIBackendLogLevel::Debug:
        Log::Debug("{}: {}", logger.source, line.text);
        break;
    case AIBackendLogLevel::Info:
        Log::Info("{}: {}", logger.source, line.text);
        break;
    case AIBackendLogLevel::Warn:
        Log::Warn("{}: {}", logger.source, line.text);
        break;
    case AIBackendLogLevel::Error:
        Log::Error("{}: {}", logger.source, line.text);
        break;
    case AIBackendLogLevel::Continue:
        break; // resolved to the preceding level by AIBackendLogBuffer
    }
}

void BackendLogCallback(ggml_log_level level, const char* text, void* userData)
{
    if (!g_logAI || !userData)
    {
        return;
    }
    auto& logger = *static_cast<BackendLogger*>(userData);
    for (const AIBackendLogLine& line : logger.buffer.Push(MapLogLevel(level), text))
    {
        EmitBackendLine(logger, line);
    }
}

void EnsureBackendInit()
{
    std::call_once(
        g_backendInit,
        []
        {
            g_logAI = AIBackendLoggingEnabled(std::getenv("FL_LOG_AI"));

            // A null callback restores upstream's stderr logger, so always install
            // our callbacks. They become a sink unless FL_LOG_AI=1 was set before
            // the first model initializes the process-global llama backend.
            llama_log_set(&BackendLogCallback, &g_llamaLogger);
            ggml_log_set(&BackendLogCallback, &g_ggmlLogger);
            mtmd_helper_log_set(&BackendLogCallback, &g_mtmdLogger);
            llama_backend_init();
        }
    );
}

std::vector<llama_token> Tokenize(const llama_vocab* vocab, const std::string& text, bool addSpecial)
{
    if (text.empty())
    {
        return {};
    }
    int n = -llama_tokenize(vocab, text.c_str(), static_cast<int>(text.size()), nullptr, 0, addSpecial, true);
    if (n <= 0)
    {
        return {};
    }
    std::vector<llama_token> out(static_cast<std::size_t>(n));
    n = llama_tokenize(vocab, text.c_str(), static_cast<int>(text.size()), out.data(), n, addSpecial, true);
    if (n < 0)
    {
        return {};
    }
    out.resize(static_cast<std::size_t>(n));
    return out;
}

std::string ApplyChatTemplate(llama_model* model, const std::string& systemPrompt, const std::string& prompt)
{
    std::vector<llama_chat_message> messages;
    if (!systemPrompt.empty())
    {
        messages.push_back({"system", systemPrompt.c_str()});
    }
    messages.push_back({"user", prompt.c_str()});
    const char* tmpl = llama_model_chat_template(model, nullptr);
    int n = llama_chat_apply_template(tmpl, messages.data(), messages.size(), true, nullptr, 0);
    if (n <= 0)
    {
        return prompt;
    }
    std::string out(static_cast<std::size_t>(n), '\0');
    n = llama_chat_apply_template(tmpl, messages.data(), messages.size(), true, out.data(), n);
    if (n > 0)
    {
        out.resize(static_cast<std::size_t>(n));
    }
    return out;
}

class LlamaEngine final : public IAIEngine
{
public:
    ~LlamaEngine() override
    {
        Reset();
    }

    bool Load(const EngineModel& spec, std::string& error) override
    {
        Reset();
        EnsureBackendInit();
        llama_model_params mp = llama_model_default_params();
        mp.n_gpu_layers = 0;
        model_ = llama_model_load_from_file(spec.modelPath.c_str(), mp);
        if (!model_)
        {
            error = "failed to load model: " + spec.modelPath;
            return false;
        }
        llama_context_params cp = llama_context_default_params();
        cp.n_ctx = static_cast<std::uint32_t>(spec.contextSize > 0 ? spec.contextSize : 4096);
        cp.n_batch = 512;
        if (spec.threads > 0)
        {
            cp.n_threads = spec.threads;
            cp.n_threads_batch = spec.threads;
        }
        nBatch_ = static_cast<int>(cp.n_batch);
        ctx_ = llama_init_from_model(model_, cp);
        if (!ctx_)
        {
            error = "failed to create llama context";
            Reset();
            return false;
        }
        vocab_ = llama_model_get_vocab(model_);
        if (!spec.projectorPath.empty())
        {
            mtmd_context_params params = mtmd_context_params_default();
            params.use_gpu = false;
            params.print_timings = false;
            if (spec.threads > 0)
            {
                params.n_threads = spec.threads;
            }
            params.media_marker = mtmd_default_marker();
            marker_ = params.media_marker ? params.media_marker : "<__media__>";
            mctx_ = mtmd_init_from_file(spec.projectorPath.c_str(), model_, params);
            if (!mctx_ || !mtmd_support_vision(mctx_))
            {
                error = "failed to load a vision-capable projector: " + spec.projectorPath;
                Reset();
                return false;
            }
        }
        return true;
    }

    void SetThreads(int threads) override
    {
        if (ctx_ && threads > 0)
        {
            llama_set_n_threads(ctx_, threads, threads);
        }
    }

    bool Generate(
        const unsigned char* rgba, int width, int height, const std::string& systemPrompt, const std::string& prompt,
        int maxTokens, float temperature, std::atomic<bool>& cancelled, std::string& text, std::string& error
    ) override
    {
        llama_pos pos = 0;
        if (!EvaluatePrefix(rgba, width, height, systemPrompt, prompt, pos, error))
        {
            return false;
        }
        llama_sampler* sampler = nullptr;
        if (temperature > 0.0f)
        {
            sampler = llama_sampler_chain_init(llama_sampler_chain_default_params());
            llama_sampler_chain_add(sampler, llama_sampler_init_temp(temperature));
            llama_sampler_chain_add(sampler, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));
        }
        else
        {
            sampler = llama_sampler_init_greedy();
        }
        text.clear();
        for (int i = 0; i < std::max(1, maxTokens) && !cancelled.load(); ++i)
        {
            const llama_token token = llama_sampler_sample(sampler, ctx_, -1);
            if (llama_vocab_is_eog(vocab_, token))
            {
                break;
            }
            char piece[256] = {};
            const int n = llama_token_to_piece(vocab_, token, piece, sizeof(piece), 0, true);
            if (n > 0)
            {
                text.append(piece, static_cast<std::size_t>(n));
            }
            if (!DecodeAt({token}, pos++))
            {
                llama_sampler_free(sampler);
                error = "llama_decode failed while generating";
                return false;
            }
        }
        llama_sampler_free(sampler);
        return !cancelled.load();
    }

    bool Score(
        const unsigned char* rgba, int width, int height, const std::string& systemPrompt, const std::string& prompt,
        const std::vector<std::string>& candidates, std::atomic<bool>& cancelled, std::vector<float>& scores,
        std::string& error
    ) override
    {
        scores.assign(candidates.size(), 0.0f);
        llama_pos pos = 0;
        if (!EvaluatePrefix(rgba, width, height, systemPrompt, prompt, pos, error))
        {
            return false;
        }
        if (cancelled.load())
        {
            return false;
        }
        std::vector<double> logScores(candidates.size(), -1e30);
        llama_memory_t memory = llama_get_memory(ctx_);
        const float* initialLogits = llama_get_logits_ith(ctx_, -1);
        if (!initialLogits)
        {
            error = "model produced no logits";
            return false;
        }
        const int vocabularySize = llama_vocab_n_tokens(vocab_);
        const std::vector<float> baseLogits(initialLogits, initialLogits + vocabularySize);
        for (std::size_t candidateIndex = 0; candidateIndex < candidates.size() && !cancelled.load(); ++candidateIndex)
        {
            const auto tokens = Tokenize(vocab_, candidates[candidateIndex], false);
            const float* logits = baseLogits.data();
            double logProbability = 0.0;
            bool valid = !tokens.empty() && logits;
            for (std::size_t tokenIndex = 0; valid && tokenIndex < tokens.size(); ++tokenIndex)
            {
                logProbability += TokenLogProbability(logits, tokens[tokenIndex]);
                if (tokenIndex + 1 < tokens.size())
                {
                    valid = DecodeAt({tokens[tokenIndex]}, pos + static_cast<llama_pos>(tokenIndex));
                    logits = valid ? llama_get_logits_ith(ctx_, -1) : nullptr;
                    valid = valid && logits;
                }
            }
            if (valid)
            {
                logScores[candidateIndex] = logProbability;
            }
            llama_memory_seq_rm(memory, 0, pos, -1);
        }
        const double maxScore = logScores.empty() ? 0.0 : *std::ranges::max_element(logScores);
        double total = 0.0;
        for (std::size_t i = 0; i < logScores.size(); ++i)
        {
            scores[i] = static_cast<float>(std::exp(logScores[i] - maxScore));
            total += scores[i];
        }
        if (total > 0.0)
        {
            for (float& score : scores)
            {
                score = static_cast<float>(score / total);
            }
        }
        return true;
    }

private:
    bool EvaluatePrefix(
        const unsigned char* rgba, int width, int height, const std::string& systemPrompt, const std::string& prompt,
        llama_pos& outPos, std::string& error
    )
    {
        if (!ctx_ || !model_)
        {
            error = "model is not loaded";
            return false;
        }
        llama_memory_seq_rm(llama_get_memory(ctx_), 0, 0, -1);
        const bool hasImage = rgba && width > 0 && height > 0;
        std::string formatted = ApplyChatTemplate(model_, systemPrompt, hasImage ? marker_ + "\n" + prompt : prompt);
        if (!hasImage)
        {
            const auto tokens = Tokenize(vocab_, formatted, true);
            if (tokens.empty() || !DecodeAt(tokens, 0))
            {
                error = "failed to evaluate prompt";
                return false;
            }
            outPos = static_cast<llama_pos>(tokens.size());
            return true;
        }
        if (!mctx_)
        {
            error = "the selected model has no vision projector";
            return false;
        }
        std::vector<unsigned char> rgb(static_cast<std::size_t>(width) * height * 3);
        for (std::size_t i = 0, count = static_cast<std::size_t>(width) * height; i < count; ++i)
        {
            rgb[i * 3] = rgba[i * 4];
            rgb[i * 3 + 1] = rgba[i * 4 + 1];
            rgb[i * 3 + 2] = rgba[i * 4 + 2];
        }
        mtmd_bitmap* bitmap =
            mtmd_bitmap_init(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), rgb.data());
        mtmd_input_chunks* chunks = mtmd_input_chunks_init();
        mtmd_input_text input{formatted.c_str(), true, true};
        const mtmd_bitmap* bitmaps[] = {bitmap};
        const int tokenized = mtmd_tokenize(mctx_, chunks, &input, bitmaps, 1);
        int evaluated = -1;
        if (tokenized == 0)
        {
            evaluated = mtmd_helper_eval_chunks(mctx_, ctx_, chunks, 0, 0, nBatch_, true, &outPos);
        }
        mtmd_input_chunks_free(chunks);
        mtmd_bitmap_free(bitmap);
        if (tokenized != 0 || evaluated != 0)
        {
            error = "failed to evaluate multimodal prompt";
            return false;
        }
        return true;
    }

    bool DecodeAt(const std::vector<llama_token>& tokens, llama_pos start)
    {
        llama_batch batch = llama_batch_init(static_cast<int>(tokens.size()), 0, 1);
        for (int i = 0; i < static_cast<int>(tokens.size()); ++i)
        {
            batch.token[i] = tokens[static_cast<std::size_t>(i)];
            batch.pos[i] = start + i;
            batch.n_seq_id[i] = 1;
            batch.seq_id[i][0] = 0;
            batch.logits[i] = i == static_cast<int>(tokens.size()) - 1;
        }
        batch.n_tokens = static_cast<int>(tokens.size());
        const int rc = llama_decode(ctx_, batch);
        llama_batch_free(batch);
        return rc == 0;
    }

    double TokenLogProbability(const float* logits, llama_token token) const
    {
        const int count = llama_vocab_n_tokens(vocab_);
        float maximum = -1e30f;
        for (int i = 0; i < count; ++i)
        {
            maximum = std::max(maximum, logits[i]);
        }
        double denominator = 0.0;
        for (int i = 0; i < count; ++i)
        {
            denominator += std::exp(static_cast<double>(logits[i] - maximum));
        }
        return static_cast<double>(logits[token] - maximum) - std::log(denominator);
    }

    void Reset()
    {
        if (mctx_)
        {
            mtmd_free(mctx_);
            mctx_ = nullptr;
        }
        if (ctx_)
        {
            llama_free(ctx_);
            ctx_ = nullptr;
        }
        if (model_)
        {
            llama_model_free(model_);
            model_ = nullptr;
        }
        vocab_ = nullptr;
    }

    llama_model* model_ = nullptr;
    llama_context* ctx_ = nullptr;
    mtmd_context* mctx_ = nullptr;
    const llama_vocab* vocab_ = nullptr;
    std::string marker_ = "<__media__>";
    int nBatch_ = 512;
};
} // namespace

std::unique_ptr<IAIEngine> CreateLlamaEngine()
{
    return std::make_unique<LlamaEngine>();
}

} // namespace hostai
