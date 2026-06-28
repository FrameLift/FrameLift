#include "BenchmarkSettings.h"
#include "Benchmark.h"

BenchmarkSettings::BenchmarkSettings(Benchmark& benchmark) : benchmark_(benchmark)
{
    SeedFromBenchmark();
}

QString BenchmarkSettings::Title() const
{
    return QStringLiteral("Benchmark");
}

bool BenchmarkSettings::Dirty() const
{
    return dirty_;
}

bool BenchmarkSettings::LimitDuration() const
{
    return limitDuration_;
}

float BenchmarkSettings::BenchmarkDuration() const
{
    return benchmarkDuration_;
}

void BenchmarkSettings::SetLimitDuration(bool value)
{
    if (limitDuration_ != value)
    {
        limitDuration_ = value;
        MarkDirty();
    }
}

void BenchmarkSettings::SetBenchmarkDuration(float value)
{
    if (benchmarkDuration_ != value)
    {
        benchmarkDuration_ = value;
        MarkDirty();
    }
}

void BenchmarkSettings::save()
{
    benchmark_.ApplySettings(limitDuration_, benchmarkDuration_);
    dirty_ = false;
    Q_EMIT changed();
}

void BenchmarkSettings::reset()
{
    limitDuration_ = false;
    benchmarkDuration_ = 30.0f;
    MarkDirty();
}

void BenchmarkSettings::load()
{
    SeedFromBenchmark();
    Q_EMIT changed();
}

void BenchmarkSettings::SeedFromBenchmark()
{
    limitDuration_ = benchmark_.limitDuration_;
    benchmarkDuration_ = benchmark_.benchmarkDuration_;
    dirty_ = false;
}

void BenchmarkSettings::MarkDirty()
{
    dirty_ = true;
    Q_EMIT changed();
}
