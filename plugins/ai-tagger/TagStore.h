#pragma once

#include "TagTypes.h"

#include <string>
#include <vector>

class IMediaStore;

// Persistence for AITagger over the shared media store (SQLite), under the `aitagger`
// namespace (every table prefixed `aitagger_`). All methods degrade to harmless no-ops
// / empty results when the store is null, so the plugin stays functional (inert) in
// lean/headless builds. Thread-safe to the extent IMediaStore is (per-thread
// connections) — the tagging worker writes from its own thread while queries come from
// the UI thread.
class TagStore
{
public:
    explicit TagStore(IMediaStore* store);

    [[nodiscard]] bool Available() const
    {
        return store_ != nullptr;
    }

    // ── File / run / tag writes (called from the worker thread) ─────────────────
    // Insert or update the file row, returning its id (0 on failure).
    [[nodiscard]] long long UpsertFile(const std::string& path, long long mtime, long long size);
    // Open a run row, returning its id (0 on failure).
    [[nodiscard]] long long BeginRun(
        long long fileId, const std::string& modelId, long long ruleId, const std::string& fingerprint
    );
    void FinishRun(long long runId, int framesSampled, int status);
    // Replace the tag rows this file+model produced with `results`.
    void WriteTags(
        long long fileId, long long runId, const std::string& modelId, const std::vector<aitagger::TagResult>& results
    );

    // True if `path` is not yet tagged, or its size/mtime changed since last tagged.
    [[nodiscard]] bool NeedsTagging(
        const std::string& path, long long mtime, long long size, const std::string& fingerprint
    );

    // ── Rules (folder → model + questions) ──────────────────────────────────────
    void UpsertRule(aitagger::TagRule& rule); // fills rule.id
    void DeleteRule(long long ruleId);
    [[nodiscard]] std::vector<aitagger::TagRule> ListRules();
    // The most specific rule whose folder is a prefix of `filePath`'s directory, if any.
    [[nodiscard]] bool RuleForFile(const std::string& filePath, aitagger::TagRule& out);

    // ── Queries backing IMediaTags (UI thread) ──────────────────────────────────
    [[nodiscard]] int TagCount(const std::string& path);
    // index-th present tag ordered by confidence desc; false if out of range.
    [[nodiscard]] bool TagAt(const std::string& path, int index, aitagger::TagResult& out);
    [[nodiscard]] bool HasTag(const std::string& path, const std::string& tag, float minConfidence);

    // Create the current pre-production schema. A version mismatch drops only the
    // AI Tagger tables; no legacy migration is maintained before release.
    void EnsureSchema();

private:
    IMediaStore* store_ = nullptr;
    void LoadRuleEntries(aitagger::TagRule& rule);
};
