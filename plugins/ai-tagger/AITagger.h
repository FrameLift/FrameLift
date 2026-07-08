#pragma once

#include "TagStore.h"
#include "TagWorker.h"

#include <framelift/core.h>
#include <framelift/services.h>

#include <QtCore/QObject>
#include <cstdint>
#include <memory>
#include <string>

class IFrameSampler;
class IEventPump;

// AI video tagging plugin. Runs local vision-language inference on frames sampled off
// the playback path (via IFrameSampler), governed by per-folder rules, and stores the
// resulting tags in the shared media store. Exposes them to other plugins through
// IMediaTags and announces updates with MediaTagsUpdatedEvent.
//
// This is the plugin core (PR B): storage, worker pipeline, inference backend, and the
// service. The settings UI, model downloader, context-menu entry points, folder
// watcher, and playback throttling arrive in a follow-up.
class AITagger : public QObject, public ModuleBase, public IMediaTags
{
    Q_OBJECT
    Q_PROPERTY(bool running READ IsRunning NOTIFY progressChanged)
    Q_PROPERTY(QString currentFile READ CurrentFile NOTIFY progressChanged)
    Q_PROPERTY(int filesDone READ FilesDone NOTIFY progressChanged)
    Q_PROPERTY(int filesTotal READ FilesTotal NOTIFY progressChanged)

public:
    AITagger();
    ~AITagger() override;

    // Queue a single file (if a folder rule covers it). No-op without a rule/model.
    Q_INVOKABLE void tagFile(const QString& path);
    // Queue every untagged video under `dir` covered by a rule.
    Q_INVOKABLE void tagFolder(const QString& dir);
    Q_INVOKABLE void cancel();

    [[nodiscard]] bool IsRunning() const;
    [[nodiscard]] QString CurrentFile() const;
    [[nodiscard]] int FilesDone() const;
    [[nodiscard]] int FilesTotal() const;

    // Test seam: inject a store + backend factory without a live IModuleContext.
    void ConfigureForTest(IMediaStore* store, IFrameSampler* sampler, aitagger::TagWorker::BackendFactory factory);

    // ── IMediaTags ──────────────────────────────────────────────────────────────
    [[nodiscard]] int GetTagCount(const char* path) const noexcept override;
    [[nodiscard]] int GetTag(
        const char* path, int index, char* buf, int cap, float* confidence, char* modelBuf, int modelCap
    ) const noexcept override;
    [[nodiscard]] bool HasTag(const char* path, const char* tag, float minConfidence) const noexcept override;

protected:
    const char* ModuleName() const override
    {
        return "AITagger";
    }

    void OnInstall(IModuleContext& ctx) override;
    bool HandleEvent(const AppEvent& e) override;

Q_SIGNALS:
    void progressChanged();

private:
    // Resolve a rule's model id to concrete GGUF paths under <exeDir>/models/. A proper
    // catalogue-backed resolver arrives with the model manager; this is the placeholder.
    bool ResolveModel(const std::string& modelId, aitagger::ModelSpec& spec) const;
    // Build a job for `path` from the covering rule, or return false.
    bool BuildJob(const std::string& path, aitagger::TagJob& job) const;
    void PublishCompleted();

    std::unique_ptr<TagStore> store_;
    std::unique_ptr<aitagger::TagWorker> worker_;
    IFrameSampler* sampler_ = nullptr;

    uint32_t progressEvent_ = 0;
    uint32_t doneEvent_ = 0;
};

FRAMELIFT_MODULE_ENTRY(
    AITagger, {
                  .qml = false,
              }
)
