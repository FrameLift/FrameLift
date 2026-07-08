#include "SampleScheduler.h"

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

ConvergenceTracker::ConvergenceTracker(const std::vector<float>& thresholds, Params params)
    : thresholds_(thresholds), maxConf_(thresholds.size(), 0.0f), lastGainSample_(thresholds.size(), 0), params_(params)
{
}

void ConvergenceTracker::Observe(const std::vector<float>& yesProb)
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
    if (conf >= thresholds_[question])
    {
        return true; // strong positive: a tag is present, further frames can't unsay it
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
