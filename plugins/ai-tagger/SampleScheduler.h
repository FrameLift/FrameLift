#pragma once

#include <array>
#include <string>
#include <vector>

#include "TagTypes.h"

// Recursive midpoint sampling schedule + per-question convergence, both pure logic
// (no FFmpeg / llama / Qt) so they are unit-testable in the FFmpeg-free test build.
//
// Sampling explores the timeline breadth-first by repeated bisection: generation k
// yields the odd multiples of d/2^(k+1) — d/2; then d/4, 3d/4; then d/8…7d/8; …. This
// gives good coverage from the very first samples and progressively refines. The worker
// evaluates one generation at a time and stops at a generation boundary once every rule
// question has converged (or the frame budget is hit).
namespace aitagger
{

struct SamplePlan
{
    std::vector<double> timestamps; // sample positions in seconds, in visit order
    std::vector<int> generationEnd; // exclusive end index of each generation into timestamps
};

// Build the sample plan for a file of `durationSec`, capped at `budget` samples.
// A non-positive duration or budget yields a single sample at t=0.
[[nodiscard]] SamplePlan BuildSamplePlan(double durationSec, int budget);

using VisualSignature = std::array<float, 16>;

// Reorder the existing interior midpoint candidates by a greedy blend of temporal
// separation and thumbnail appearance difference. No new edge timestamps are added.
[[nodiscard]] SamplePlan RankAdaptiveSamples(
    const std::vector<double>& timestamps, const std::vector<VisualSignature>& signatures, double durationSec
);

void InputSize(int nativeW, int nativeH, int maxInputSide, int& outW, int& outH);

[[nodiscard]] std::string BuildTaggingFingerprint(
    const std::string& modelId, const std::string& modelRevision, const std::vector<RuleEntry>& entries,
    float ruleThreshold, int frameBudget, int maxInputSide
);

// Tracks the running yes-probability aggregate per question and decides when each has
// converged. Confidence is monotone: we keep the max yes-probability seen (one strong
// positive is enough to declare a tag present; later frames only confirm).
class ConvergenceTracker
{
public:
    struct Params
    {
        int minSamples = 7;       // don't settle a question before this many observations
        float negCeiling = 0.25f; // maxConf at/below this after minSamples ⇒ confident negative
        int stableWindow = 4;     // observations without a meaningful gain ⇒ plateaued
        float epsilon = 0.05f;    // "meaningful gain" in maxConf
    };

    ConvergenceTracker(const std::vector<float>& thresholds, Params params);

    // Feed one frame's per-question yes-probabilities (same order/size as thresholds).
    void Observe(const std::vector<float>& yesProb, double timestamp = 0.0);

    [[nodiscard]] bool Settled(int question) const;
    [[nodiscard]] bool AllSettled() const;
    [[nodiscard]] float MaxConf(int question) const;
    [[nodiscard]] int SupportCount(int question) const;
    [[nodiscard]] double BestTimestamp(int question) const;
    [[nodiscard]] bool Present(int question) const;

    [[nodiscard]] int SampleCount() const
    {
        return sampleCount_;
    }

private:
    std::vector<float> thresholds_;
    std::vector<float> maxConf_;
    std::vector<int> lastGainSample_; // observation index at which maxConf last jumped ≥ epsilon
    std::vector<int> supportCount_;
    std::vector<double> bestTimestamp_;
    Params params_;
    int sampleCount_ = 0;
};

} // namespace aitagger
