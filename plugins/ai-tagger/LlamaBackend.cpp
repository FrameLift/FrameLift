#include "InferenceBackend.h"

#include <framelift/Log.h>

#include <llama.h>
#include <mtmd-helper.h>
#include <mtmd.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

// The ONLY translation unit that includes llama/mtmd headers. Everything else in the
// plugin talks to IInferenceBackend, so the pure-logic worker/store/scheduler stay
// testable in the FFmpeg-/llama-free test build.
namespace aitagger
{
namespace
{
std::once_flag g_backendInit;

void EnsureBackendInit()
{
    std::call_once(
        g_backendInit,
        []
        {
            llama_backend_init();
        }
    );
}

// Tokenize `text`; add_special adds BOS/EOS per the model config.
std::vector<llama_token> Tokenize(const llama_vocab* vocab, const std::string& text, bool addSpecial)
{
    if (text.empty())
    {
        return {};
    }
    int n = -llama_tokenize(vocab, text.c_str(), static_cast<int>(text.size()), nullptr, 0, addSpecial, true);
    std::vector<llama_token> out(static_cast<std::size_t>(n));
    n = llama_tokenize(vocab, text.c_str(), static_cast<int>(text.size()), out.data(), n, addSpecial, true);
    if (n < 0)
    {
        return {};
    }
    out.resize(static_cast<std::size_t>(n));
    return out;
}

class LlamaBackend final : public IInferenceBackend
{
public:
    ~LlamaBackend() override
    {
        UnloadModel();
    }

    bool LoadModel(const ModelSpec& spec, std::string& err) override
    {
        UnloadModel();
        EnsureBackendInit();

        if (spec.mmprojPath.empty())
        {
            err = "mmproj (vision projector) path is required for a vision model";
            return false;
        }

        llama_model_params mp = llama_model_default_params();
        mp.n_gpu_layers = 0; // v1: CPU only
        model_ = llama_model_load_from_file(spec.modelPath.c_str(), mp);
        if (!model_)
        {
            err = "failed to load model: " + spec.modelPath;
            return false;
        }

        nThreads_ = spec.nThreads > 0 ? spec.nThreads : 0;
        llama_context_params cp = llama_context_default_params();
        cp.n_ctx = static_cast<uint32_t>(spec.nCtx > 0 ? spec.nCtx : 4096);
        cp.n_batch = 512;
        if (nThreads_ > 0)
        {
            cp.n_threads = nThreads_;
            cp.n_threads_batch = nThreads_;
        }
        nBatch_ = static_cast<int>(cp.n_batch);
        ctx_ = llama_init_from_model(model_, cp);
        if (!ctx_)
        {
            err = "failed to create llama context";
            UnloadModel();
            return false;
        }

        mtmd_context_params mparams = mtmd_context_params_default();
        mparams.use_gpu = false;
        mparams.print_timings = false;
        if (nThreads_ > 0)
        {
            mparams.n_threads = nThreads_;
        }
        mparams.media_marker = mtmd_default_marker();
        marker_ = mparams.media_marker ? mparams.media_marker : "<__media__>";
        mctx_ = mtmd_init_from_file(spec.mmprojPath.c_str(), model_, mparams);
        if (!mctx_)
        {
            err = "failed to load mmproj: " + spec.mmprojPath;
            UnloadModel();
            return false;
        }
        if (!mtmd_support_vision(mctx_))
        {
            err = "mmproj does not provide vision support";
            UnloadModel();
            return false;
        }

        vocab_ = llama_model_get_vocab(model_);
        BuildYesNoSets();
        return true;
    }

    void UnloadModel() override
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
        yesIds_.clear();
        noIds_.clear();
    }

    void SetThreads(int nThreads) override
    {
        nThreads_ = nThreads;
        if (ctx_ && nThreads > 0)
        {
            llama_set_n_threads(ctx_, nThreads, nThreads);
        }
    }

    bool EvaluateFrame(
        const std::uint8_t* rgba, int w, int h, const std::vector<BackendQuestion>& questions, std::vector<float>& out,
        std::string& err
    ) override
    {
        out.assign(questions.size(), 0.0f);
        if (!ctx_ || !mctx_ || !model_ || w <= 0 || h <= 0 || !rgba)
        {
            err = "backend not loaded";
            return false;
        }

        // RGBA → RGB (mtmd bitmaps are 3-channel).
        std::vector<unsigned char> rgb(static_cast<std::size_t>(w) * h * 3);
        for (std::size_t i = 0, px = static_cast<std::size_t>(w) * h; i < px; ++i)
        {
            rgb[i * 3 + 0] = rgba[i * 4 + 0];
            rgb[i * 3 + 1] = rgba[i * 4 + 1];
            rgb[i * 3 + 2] = rgba[i * 4 + 2];
        }
        mtmd_bitmap* bmp = mtmd_bitmap_init(static_cast<uint32_t>(w), static_cast<uint32_t>(h), rgb.data());
        if (!bmp)
        {
            err = "mtmd_bitmap_init failed";
            return false;
        }

        // Encode a system + image user turn once; reuse it for every question by
        // rewinding the KV cache to the position right after it.
        const std::string prefix = "<|im_start|>system\nAnswer each question about the image with only "
                                   "yes or no.<|im_end|>\n<|im_start|>user\n" +
                                   marker_ + "<|im_end|>\n";
        mtmd_input_text itext{prefix.c_str(), /*add_special=*/true, /*parse_special=*/true};
        mtmd_input_chunks* chunks = mtmd_input_chunks_init();
        const mtmd_bitmap* bmps[1] = {bmp};
        const int32_t trc = mtmd_tokenize(mctx_, chunks, &itext, bmps, 1);
        if (trc != 0)
        {
            mtmd_input_chunks_free(chunks);
            mtmd_bitmap_free(bmp);
            err = "mtmd_tokenize failed (" + std::to_string(trc) + ")";
            return false;
        }

        llama_memory_t mem = llama_get_memory(ctx_);
        llama_memory_seq_rm(mem, 0, 0, -1); // clean slate

        llama_pos p0 = 0;
        const int32_t erc = mtmd_helper_eval_chunks(
            mctx_, ctx_, chunks, /*n_past=*/0, /*seq_id=*/0, nBatch_,
            /*logits_last=*/false, &p0
        );
        mtmd_input_chunks_free(chunks);
        mtmd_bitmap_free(bmp);
        if (erc != 0)
        {
            err = "mtmd_helper_eval_chunks failed (" + std::to_string(erc) + ")";
            return false;
        }

        for (std::size_t q = 0; q < questions.size(); ++q)
        {
            const std::string qtext =
                "<|im_start|>user\n" + questions[q].question + "<|im_end|>\n<|im_start|>assistant\n";
            const std::vector<llama_token> toks = Tokenize(vocab_, qtext, /*add_special=*/false);
            if (toks.empty())
            {
                continue;
            }
            if (!DecodeAt(toks, p0))
            {
                err = "llama_decode failed";
                return false;
            }
            const float* logits = llama_get_logits_ith(ctx_, static_cast<int32_t>(toks.size()) - 1);
            out[q] = logits ? YesProbability(logits) : 0.0f;
            llama_memory_seq_rm(mem, 0, p0, -1); // rewind for the next question
        }
        return true;
    }

private:
    void AddFirstToken(const std::string& s, std::vector<llama_token>& into)
    {
        const std::vector<llama_token> t = Tokenize(vocab_, s, false);
        if (!t.empty() && std::find(into.begin(), into.end(), t.front()) == into.end())
        {
            into.push_back(t.front());
        }
    }

    void BuildYesNoSets()
    {
        yesIds_.clear();
        noIds_.clear();
        for (const char* s : {"yes", " yes", "Yes", " Yes", "YES", " YES"})
        {
            AddFirstToken(s, yesIds_);
        }
        for (const char* s : {"no", " no", "No", " No", "NO", " NO"})
        {
            AddFirstToken(s, noIds_);
        }
    }

    bool DecodeAt(const std::vector<llama_token>& toks, llama_pos startPos)
    {
        const int n = static_cast<int>(toks.size());
        llama_batch batch = llama_batch_init(n, 0, 1);
        for (int i = 0; i < n; ++i)
        {
            batch.token[i] = toks[static_cast<std::size_t>(i)];
            batch.pos[i] = startPos + i;
            batch.n_seq_id[i] = 1;
            batch.seq_id[i][0] = 0;
            batch.logits[i] = (i == n - 1) ? 1 : 0;
        }
        batch.n_tokens = n;
        const int rc = llama_decode(ctx_, batch);
        llama_batch_free(batch);
        return rc == 0;
    }

    // P(yes) = softmax mass of the yes-token family over yes ∪ no.
    float YesProbability(const float* logits) const
    {
        float maxL = -1e30f;
        for (llama_token t : yesIds_)
        {
            maxL = std::max(maxL, logits[t]);
        }
        for (llama_token t : noIds_)
        {
            maxL = std::max(maxL, logits[t]);
        }
        double sumYes = 0.0;
        double sumNo = 0.0;
        for (llama_token t : yesIds_)
        {
            sumYes += std::exp(static_cast<double>(logits[t] - maxL));
        }
        for (llama_token t : noIds_)
        {
            sumNo += std::exp(static_cast<double>(logits[t] - maxL));
        }
        const double denom = sumYes + sumNo;
        return denom > 0.0 ? static_cast<float>(sumYes / denom) : 0.0f;
    }

    llama_model* model_ = nullptr;
    llama_context* ctx_ = nullptr;
    mtmd_context* mctx_ = nullptr;
    const llama_vocab* vocab_ = nullptr;
    std::string marker_ = "<__media__>";
    int nThreads_ = 0;
    int nBatch_ = 512;
    std::vector<llama_token> yesIds_;
    std::vector<llama_token> noIds_;
};
} // namespace

std::unique_ptr<IInferenceBackend> CreateLlamaBackend()
{
    return std::make_unique<LlamaBackend>();
}

} // namespace aitagger
