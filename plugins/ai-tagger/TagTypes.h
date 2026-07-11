#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Plain data types shared across the AITagger plugin's internal modules. These are
// plugin-internal (not ABI), so std types are fine.
namespace aitagger
{

enum class AnalysisMode : std::uint8_t
{
    Auto,
    FullFrame,
    HumanDetail
};

// One yes/no question and the tag it produces, e.g. { "Does this scene contain a
// beach?", "beach" }. A negative per-entry threshold means "use the rule default".
struct RuleEntry
{
    std::string question;
    std::string tag;
    float threshold = -1.0f;
    AnalysisMode analysisMode = AnalysisMode::Auto;
};

// A per-folder tagging rule: which model, which questions/tags, and run parameters.
struct TagRule
{
    long long id = 0;
    std::string folder;  // absolute directory this rule applies to
    std::string modelId; // catalogue id or installed model id
    float threshold = 0.6f;
    int frameBudget = 31;
    bool watch = false;
    std::vector<RuleEntry> entries;

    // Effective threshold for an entry (its own if set, else the rule default).
    [[nodiscard]] float EntryThreshold(const RuleEntry& e) const
    {
        return e.threshold >= 0.0f ? e.threshold : threshold;
    }
};

// A produced tag row for one file.
struct TagResult
{
    std::string tag;
    float confidence = 0.0f;
    std::string modelId;
    bool present = false;
    int supportCount = 0;
    double bestTimestamp = 0.0;
};

} // namespace aitagger
