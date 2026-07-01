#include "Benchmark.h"
#include "SysStats.h"

#include "QtTestRunner.h"

#include <QtTest/QtTest>

class BenchmarkTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void TogglesVisibility()
    {
        Benchmark b;
        QVERIFY(!(b.IsOpen())); // closed by default
        b.Toggle();
        QVERIFY(b.IsOpen());
        b.Toggle();
        QVERIFY(!(b.IsOpen()));
    }

    void MediaEventsDoNotCrashWhileClosed()
    {
        Benchmark b;
        MediaEvent e;
        e.type = MediaEventType::PropertyChange;
        e.property.prop = PlayerProperty::IdleActive;
        e.property.type = PropertyType::Flag;
        e.property.value.flag = 1;
        b.OnMediaEvent(e); // must be safe with no player/context
    }

    void SamplerReportsNormalizedValues()
    {
        SysSampler sampler;
        const SysSample first = sampler.Sample(); // primes the rate counters
        const SysSample second = sampler.Sample();

        // CPU is normalized across cores and clamped; memory is non-negative.
        QVERIFY((second.cpuPercent) >= (0.0));
        QVERIFY((second.cpuPercent) <= (100.0));
        QVERIFY((first.memBytes) >= (0u));

        // GPU is best-effort: when reported it must be a sane percentage.
        if (second.gpuValid)
        {
            QVERIFY((second.gpuPercent) >= (0.0));
            QVERIFY((second.gpuPercent) <= (100.0));
        }
    }
};

namespace
{
const ::framelift::test::Registrar<BenchmarkTest> kRegisterBenchmarkTest{"BenchmarkTest"};
}

#include "BenchmarkTests.moc"
