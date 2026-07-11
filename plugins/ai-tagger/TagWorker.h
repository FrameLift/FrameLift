#pragma once

#include "InferenceBackend.h"
#include "TagTypes.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class IFrameSampler;
class IEventPump;
class TagStore;

// Background tagging pipeline: sample frames → infer → store, following the Playlist
// async-scan pattern (a heap-owned control block that outlives the worker via
// shared_ptr, a generation/alive guard, results marshalled to the UI thread with a
// custom event). The pure core RunSync() is directly testable with a fake sampler and
// fake backend; Start() wraps it in a detached thread (or runs inline when there is no
// event pump, for headless/test determinism).
namespace aitagger
{

// One file to tag with a fully-resolved model + question set (the plugin resolves a
// rule's model id to concrete GGUF paths before enqueuing, keeping the worker pure).
struct TagJob
{
    std::string path;
    long long ruleId = 0;
    ModelSpec model;
    std::vector<RuleEntry> entries; // question → tag (+ optional per-entry threshold)
    float ruleThreshold = 0.6f;
    int frameBudget = 31;
    int maxInputSide = 768;
    std::string fingerprint;
};

struct TagProgress
{
    bool running = false;
    std::string currentFile;
    int filesDone = 0;
    int filesTotal = 0;
    int framesDone = 0;
    int framesTotal = 0;
};

class TagWorker
{
public:
    using BackendFactory = std::function<std::unique_ptr<IInferenceBackend>()>;

    TagWorker(IFrameSampler* sampler, TagStore* store, BackendFactory factory);
    ~TagWorker();

    TagWorker(const TagWorker&) = delete;
    TagWorker& operator=(const TagWorker&) = delete;

    // Marshalling config; without an event pump Start() runs synchronously.
    void Configure(IEventPump* events, uint32_t progressEvent, uint32_t doneEvent);

    // Queue jobs and run them (detached thread if an event pump is set, else inline).
    void Start(std::vector<TagJob> jobs);

    // Cancel the in-flight run (cooperative; checked between frames/files).
    void Cancel();

    // Live throttle: nThreads<=0 restores full speed, >0 caps the backend thread count
    // and adds an inter-frame pause while playback is active.
    void SetThrottle(int nThreads);

    // UI thread pulls completed file paths (to publish MediaTagsUpdatedEvent).
    [[nodiscard]] std::vector<std::string> DrainCompleted();
    [[nodiscard]] TagProgress GetProgress() const;

    // Synchronous core — runs the whole job list on the calling thread. Public for
    // tests; Start() calls it on the worker thread.
    void RunSync(const std::vector<TagJob>& jobs);

private:
    // Everything shared with a possibly-detached worker thread; outlives *this.
    struct Shared
    {
        std::mutex mtx;
        std::atomic<bool> alive{true};
        std::atomic<bool> cancel{false};
        std::atomic<int> throttle{0};
        std::atomic<uint64_t> latestGen{0};

        IFrameSampler* sampler = nullptr;
        TagStore* store = nullptr;
        BackendFactory factory;

        IEventPump* events = nullptr;
        uint32_t progressEvent = 0;
        uint32_t doneEvent = 0;

        // Guarded by mtx.
        std::vector<std::string> completed;
        TagProgress progress;
    };

    static void RunJobs(const std::shared_ptr<Shared>& sh, std::vector<TagJob> jobs, uint64_t gen);
    // Tag one file; returns frames sampled (writes rows into the store). status set out.
    static int TagOneFile(
        const std::shared_ptr<Shared>& sh, IInferenceBackend* backend, const TagJob& job, int& status
    );

    std::shared_ptr<Shared> shared_ = std::make_shared<Shared>();
};

} // namespace aitagger
