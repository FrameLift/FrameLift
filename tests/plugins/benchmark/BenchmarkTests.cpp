#include "Benchmark.h"
#include "SysStats.h"

#include <gtest/gtest.h>

TEST(BenchmarkTest, TogglesVisibility)
{
    Benchmark b;
    EXPECT_FALSE(b.IsOpen()); // closed by default
    b.Toggle();
    EXPECT_TRUE(b.IsOpen());
    b.Toggle();
    EXPECT_FALSE(b.IsOpen());
}

TEST(BenchmarkTest, MediaEventsDoNotCrashWhileClosed)
{
    Benchmark b;
    MediaEvent e;
    e.type = MediaEventType::PropertyChange;
    e.property.prop = PlayerProperty::IdleActive;
    e.property.type = PropertyType::Flag;
    e.property.value.flag = 1;
    b.OnMediaEvent(e); // must be safe with no player/context
    SUCCEED();
}

TEST(BenchmarkTest, SamplerReportsNormalizedValues)
{
    SysSampler sampler;
    const SysSample first = sampler.Sample(); // primes the rate counters
    const SysSample second = sampler.Sample();

    // CPU is normalized across cores and clamped; memory is non-negative.
    EXPECT_GE(second.cpuPercent, 0.0);
    EXPECT_LE(second.cpuPercent, 100.0);
    EXPECT_GE(first.memBytes, 0u);

    // GPU is best-effort: when reported it must be a sane percentage.
    if (second.gpuValid)
    {
        EXPECT_GE(second.gpuPercent, 0.0);
        EXPECT_LE(second.gpuPercent, 100.0);
    }
}
