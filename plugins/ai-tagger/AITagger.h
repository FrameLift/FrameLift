#pragma once

#include "TagStore.h"
#include "TagWorker.h"

#include <framelift/core.h>
#include <framelift/services.h>

#include <QtCore/QFileSystemWatcher>
#include <QtCore/QObject>
#include <QtCore/QTimer>
#include <cstdint>
#include <memory>
#include <string>

class IFrameSampler;
class IEventPump;
class AITaggerSettings;

// AI video tagging plugin. Runs local vision-language inference on frames sampled off
// the playback path (via IFrameSampler), governed by per-folder rules, and stores the
// resulting tags in the shared media store. Exposes them to other plugins through
// IMediaTags and announces updates with MediaTagsUpdatedEvent. Entry points: a settings
// page (rules + model manager), context-menu "Tag this file / folder", and an optional
// per-rule folder watcher. Background tagging throttles down while playback is active.
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

    // Re-read rules and (re)arm the folder watchers. Called by the settings page after
    // a rule is added/removed.
    void OnRulesChanged();

    // Store access for the settings page (host-owned store; may be null).
    [[nodiscard]] TagStore* StoreForSettings()
    {
        return store_.get();
    }

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
    void HandleMediaEvent(const MediaEvent& e) override;

Q_SIGNALS:
    void progressChanged();

private:
    // Resolve a rule's model id to concrete GGUF paths under <exeDir>/models/.
    bool ResolveModel(const std::string& modelId, aitagger::ModelSpec& spec) const;
    // Build a job for `path` from the covering rule, or return false.
    bool BuildJob(const std::string& path, aitagger::TagJob& job) const;
    void PublishCompleted();
    // Folder watching for rules with watch=true.
    void ArmWatchers();
    // Debounced throttle: playback active ⇒ throttle; restore only after a quiet period
    // so a brief stop→play (playlist file change) never un-throttles.
    void SetPlaybackActive(bool active);

    std::unique_ptr<TagStore> store_;
    std::unique_ptr<aitagger::TagWorker> worker_;
    std::unique_ptr<AITaggerSettings> settingsPage_;
    IFrameSampler* sampler_ = nullptr;

    uint32_t progressEvent_ = 0;
    uint32_t doneEvent_ = 0;
    uint32_t dirChangedEvent_ = 0;

    std::string currentFile_;
    QFileSystemWatcher dirWatcher_;
    QTimer throttleRestoreTimer_; // single-shot; fires SetThrottle(0)
    bool playbackActive_ = false;
};

FRAMELIFT_MODULE_ENTRY(
    AITagger, {
                  .renderOrder = 40,
              }
)
