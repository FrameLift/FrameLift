#include "LogRepeatCollapse.h"

#include "QtTestRunner.h"

#include <QtTest/QtTest>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

// Pure-logic tests for the repeat-storm collapser behind the FFmpeg log callback.
// No libav — the callback owns locking and level mapping; this type owns the
// suppress/fold decisions.

using namespace std::chrono_literals;

namespace
{
struct EmittedLine
{
    int level = 0;
    std::string message;
    std::uint64_t occurrences = 0;
};

struct Recorder
{
    std::vector<EmittedLine> lines;

    auto Sink()
    {
        return [this](int level, const char* message, std::uint64_t occurrences)
        {
            lines.push_back({level, message, occurrences});
        };
    }
};

constexpr auto kWindow = 3s;
constexpr int kError = 16; // AV_LOG_ERROR value; the collapser treats levels opaquely
constexpr int kWarn = 24;
} // namespace

class LogRepeatCollapseTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void firstOccurrenceEmitsImmediately()
    {
        LogRepeatCollapser c(kWindow);
        Recorder rec;
        const auto t0 = LogRepeatCollapser::Clock::time_point{};

        c.Observe(kError, "Header missing", t0, rec.Sink());

        QCOMPARE(rec.lines.size(), std::size_t{1});
        QCOMPARE(rec.lines[0].message, std::string("Header missing"));
        QCOMPARE(rec.lines[0].occurrences, std::uint64_t{1});
        QCOMPARE(rec.lines[0].level, kError);
    }

    void repeatsInsideWindowAreSuppressed()
    {
        LogRepeatCollapser c(kWindow);
        Recorder rec;
        const auto t0 = LogRepeatCollapser::Clock::time_point{};

        c.Observe(kError, "Header missing", t0, rec.Sink());
        for (int i = 1; i <= 20; ++i)
        {
            c.Observe(kError, "Header missing", t0 + std::chrono::milliseconds(100 * i), rec.Sink());
        }

        QCOMPARE(rec.lines.size(), std::size_t{1}); // only the first line
    }

    void sameMessageAfterWindowFoldsTheTally()
    {
        LogRepeatCollapser c(kWindow);
        Recorder rec;
        const auto t0 = LogRepeatCollapser::Clock::time_point{};

        c.Observe(kError, "Header missing", t0, rec.Sink());
        for (int i = 1; i <= 10; ++i)
        {
            c.Observe(kError, "Header missing", t0 + std::chrono::milliseconds(100 * i), rec.Sink());
        }
        c.Observe(kError, "Header missing", t0 + kWindow + 1ms, rec.Sink());

        QCOMPARE(rec.lines.size(), std::size_t{2});
        // 10 suppressed + the occurrence that reopened the window
        QCOMPARE(rec.lines[1].occurrences, std::uint64_t{11});
        QCOMPARE(rec.lines[1].message, std::string("Header missing"));
    }

    void differentMessageFlushesPendingTallyFirst()
    {
        LogRepeatCollapser c(kWindow);
        Recorder rec;
        const auto t0 = LogRepeatCollapser::Clock::time_point{};

        c.Observe(kError, "Header missing", t0, rec.Sink());
        c.Observe(kError, "Header missing", t0 + 100ms, rec.Sink());
        c.Observe(kError, "Header missing", t0 + 200ms, rec.Sink());
        c.Observe(kWarn, "deprecated pixel format", t0 + 300ms, rec.Sink());

        QCOMPARE(rec.lines.size(), std::size_t{3});
        // The flush line carries the previous message's level and only the suppressed count.
        QCOMPARE(rec.lines[1].message, std::string("Header missing"));
        QCOMPARE(rec.lines[1].occurrences, std::uint64_t{2});
        QCOMPARE(rec.lines[1].level, kError);
        QCOMPARE(rec.lines[2].message, std::string("deprecated pixel format"));
        QCOMPARE(rec.lines[2].occurrences, std::uint64_t{1});
        QCOMPARE(rec.lines[2].level, kWarn);
    }

    void differentMessageWithoutPendingEmitsOnce()
    {
        LogRepeatCollapser c(kWindow);
        Recorder rec;
        const auto t0 = LogRepeatCollapser::Clock::time_point{};

        c.Observe(kError, "one", t0, rec.Sink());
        c.Observe(kError, "two", t0 + 1ms, rec.Sink());
        c.Observe(kError, "three", t0 + 2ms, rec.Sink());

        QCOMPARE(rec.lines.size(), std::size_t{3});
        for (const EmittedLine& line : rec.lines)
        {
            QCOMPARE(line.occurrences, std::uint64_t{1});
        }
    }

    void alternatingSlowMessagesNeverFold()
    {
        LogRepeatCollapser c(kWindow);
        Recorder rec;
        auto t = LogRepeatCollapser::Clock::time_point{};

        for (int i = 0; i < 6; ++i)
        {
            c.Observe(kError, i % 2 == 0 ? "a" : "b", t, rec.Sink());
            t += kWindow + 1ms;
        }

        QCOMPARE(rec.lines.size(), std::size_t{6});
        for (const EmittedLine& line : rec.lines)
        {
            QCOMPARE(line.occurrences, std::uint64_t{1});
        }
    }
};

namespace
{
const ::framelift::test::Registrar<LogRepeatCollapseTest> kRegisterLogRepeatCollapseTest{"LogRepeatCollapseTest"};
}

#include "LogRepeatCollapseTests.moc"
