#include "SampleScheduler.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace aitagger
{

SamplePlan BuildSamplePlan(double durationSec, int budget)
{
    SamplePlan plan;
    if (durationSec <= 0.0 || budget <= 0)
    {
        plan.timestamps.push_back(0.0);
        plan.generationEnd.push_back(1);
        return plan;
    }

    // Generation k contributes 2^k samples at (2i-1)/2^(k+1) * duration, i = 1..2^k.
    int countInGen = 1;
    for (int gen = 0; static_cast<int>(plan.timestamps.size()) < budget; ++gen)
    {
        const double denom = static_cast<double>(1u << (gen + 1));
        for (int i = 1; i <= countInGen && static_cast<int>(plan.timestamps.size()) < budget; ++i)
        {
            const double frac = static_cast<double>(2 * i - 1) / denom;
            plan.timestamps.push_back(frac * durationSec);
        }
        plan.generationEnd.push_back(static_cast<int>(plan.timestamps.size()));
        countInGen *= 2;
    }
    return plan;
}

void InputSize(int nativeW, int nativeH, int maxInputSide, int& outW, int& outH)
{
    if (nativeW <= 0 || nativeH <= 0)
    {
        outW = outH = 0;
        return;
    }
    const int cap = std::clamp(maxInputSide, 1, 8192);
    const int longSide = std::max(nativeW, nativeH);
    if (longSide <= cap)
    {
        outW = nativeW;
        outH = nativeH;
        return;
    }
    const double scale = static_cast<double>(cap) / longSide;
    outW = std::max(1, static_cast<int>(nativeW * scale));
    outH = std::max(1, static_cast<int>(nativeH * scale));
}

SamplePlan RankAdaptiveSamples(
    const std::vector<double>& timestamps, const std::vector<VisualSignature>& signatures, double durationSec
)
{
    SamplePlan out;
    if (timestamps.empty())
    {
        return out;
    }
    std::vector<bool> used(timestamps.size(), false);
    std::vector<std::size_t> order;
    order.reserve(timestamps.size());
    const auto center = static_cast<std::size_t>(std::distance(
        timestamps.begin(), std::min_element(
                                timestamps.begin(), timestamps.end(),
                                [durationSec](double a, double b)
                                {
                                    return std::abs(a - durationSec * 0.5) < std::abs(b - durationSec * 0.5);
                                }
                            )
    ));
    order.push_back(center);
    used[center] = true;
    while (order.size() < timestamps.size())
    {
        float bestScore = -1.0f;
        std::size_t best = 0;
        for (std::size_t i = 0; i < timestamps.size(); ++i)
        {
            if (used[i])
            {
                continue;
            }
            double temporal = 1.0;
            float visual = 1.0f;
            for (std::size_t chosen : order)
            {
                temporal =
                    std::min(temporal, std::abs(timestamps[i] - timestamps[chosen]) / std::max(0.001, durationSec));
                if (i < signatures.size() && chosen < signatures.size())
                {
                    float diff = 0.0f;
                    for (std::size_t cell = 0; cell < signatures[i].size(); ++cell)
                    {
                        diff += std::abs(signatures[i][cell] - signatures[chosen][cell]);
                    }
                    visual = std::min(visual, diff / static_cast<float>(signatures[i].size()));
                }
            }
            const float score = static_cast<float>(temporal) * 0.45f + visual * 0.55f;
            if (score > bestScore)
            {
                bestScore = score;
                best = i;
            }
        }
        used[best] = true;
        order.push_back(best);
    }
    for (std::size_t i : order)
    {
        out.timestamps.push_back(timestamps[i]);
    }
    for (int end = 7; end < static_cast<int>(out.timestamps.size()); end = end == 7 ? 15 : end * 2 + 1)
    {
        out.generationEnd.push_back(end);
    }
    if (out.generationEnd.empty() || out.generationEnd.back() != static_cast<int>(out.timestamps.size()))
    {
        out.generationEnd.push_back(static_cast<int>(out.timestamps.size()));
    }
    return out;
}

std::string BuildTaggingFingerprint(
    const std::string& modelId, const std::string& modelRevision, const std::vector<RuleEntry>& entries,
    float ruleThreshold, int frameBudget, int maxInputSide
)
{
    std::uint64_t hash = 1469598103934665603ull;
    const auto add = [&hash](const void* data, std::size_t size)
    {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < size; ++i)
        {
            hash ^= bytes[i];
            hash *= 1099511628211ull;
        }
    };
    const auto addString = [&add](const std::string& value)
    {
        add(value.data(), value.size());
        const unsigned char separator = 0xff;
        add(&separator, 1);
    };
    addString("aitagger-v2-adaptive");
    addString(modelId);
    addString(modelRevision);
    add(&ruleThreshold, sizeof(ruleThreshold));
    add(&frameBudget, sizeof(frameBudget));
    add(&maxInputSide, sizeof(maxInputSide));
    for (const auto& entry : entries)
    {
        addString(entry.question);
        addString(entry.tag);
        add(&entry.threshold, sizeof(entry.threshold));
        add(&entry.analysisMode, sizeof(entry.analysisMode));
    }
    std::ostringstream stream;
    stream << std::hex << std::setw(16) << std::setfill('0') << hash;
    return stream.str();
}

ConvergenceTracker::ConvergenceTracker(const std::vector<float>& thresholds, Params params)
    : thresholds_(thresholds), maxConf_(thresholds.size(), 0.0f), lastGainSample_(thresholds.size(), 0),
      supportCount_(thresholds.size(), 0), bestTimestamp_(thresholds.size(), 0.0), params_(params)
{
}

void ConvergenceTracker::Observe(const std::vector<float>& yesProb, double timestamp)
{
    ++sampleCount_;
    const int n = static_cast<int>(maxConf_.size());
    for (int q = 0; q < n && q < static_cast<int>(yesProb.size()); ++q)
    {
        if (yesProb[q] >= maxConf_[q] + params_.epsilon)
        {
            lastGainSample_[q] = sampleCount_;
        }
        if (yesProb[q] > maxConf_[q])
        {
            maxConf_[q] = yesProb[q];
            bestTimestamp_[q] = timestamp;
        }
        if (yesProb[q] >= thresholds_[q])
        {
            ++supportCount_[q];
        }
    }
}

bool ConvergenceTracker::Settled(int question) const
{
    if (question < 0 || question >= static_cast<int>(maxConf_.size()))
    {
        return true;
    }
    const float conf = maxConf_[question];
    const float strongThreshold = std::max(0.85f, thresholds_[question] + 0.20f);
    if (conf >= strongThreshold || supportCount_[question] >= 2)
    {
        return true;
    }
    if (sampleCount_ < params_.minSamples)
    {
        return false;
    }
    if (conf <= params_.negCeiling)
    {
        return true; // confident negative
    }
    return (sampleCount_ - lastGainSample_[question]) >= params_.stableWindow; // plateaued mid-confidence
}

int ConvergenceTracker::SupportCount(int question) const
{
    return question >= 0 && question < static_cast<int>(supportCount_.size()) ? supportCount_[question] : 0;
}

double ConvergenceTracker::BestTimestamp(int question) const
{
    return question >= 0 && question < static_cast<int>(bestTimestamp_.size()) ? bestTimestamp_[question] : 0.0;
}

bool ConvergenceTracker::Present(int question) const
{
    if (question < 0 || question >= static_cast<int>(maxConf_.size()))
    {
        return false;
    }
    return maxConf_[question] >= std::max(0.85f, thresholds_[question] + 0.20f) || supportCount_[question] >= 2;
}

bool ConvergenceTracker::AllSettled() const
{
    for (int q = 0; q < static_cast<int>(maxConf_.size()); ++q)
    {
        if (!Settled(q))
        {
            return false;
        }
    }
    return true;
}

float ConvergenceTracker::MaxConf(int question) const
{
    if (question < 0 || question >= static_cast<int>(maxConf_.size()))
    {
        return 0.0f;
    }
    return maxConf_[question];
}

} // namespace aitagger
