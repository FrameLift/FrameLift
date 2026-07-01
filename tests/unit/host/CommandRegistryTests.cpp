#include "CommandRegistry.h"

#include "QtTestRunner.h"
#include <QtTest/QtTest>
#include <string>
#include <vector>

namespace
{
struct OutputCapture
{
    std::vector<std::string> lines;
    std::vector<int> levels;
};

void CaptureOutput(void* ud, const int level, const char* text) noexcept
{
    auto* capture = static_cast<OutputCapture*>(ud);
    capture->levels.push_back(level);
    capture->lines.emplace_back(text ? text : "");
}

void EchoHandler(const ICommandRegistry::Invocation* invocation, void*) noexcept
{
    if (!invocation || !invocation->output)
    {
        return;
    }
    for (int i = 0; i < invocation->argc; ++i)
    {
        invocation->output(invocation->outputUd, ICommandRegistry::OutputInfo, invocation->argv[i]);
    }
}
} // namespace

class CommandRegistryTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void TokenizeSupportsQuotedPaths()
    {
        const auto parsed = CommandRegistry::Tokenize("open-file \"/foo bar/movie.mp4\"");

        QVERIFY(parsed.error.empty());
        QCOMPARE(parsed.args.size(), 2u);
        QCOMPARE(parsed.args[0], std::string("open-file"));
        QCOMPARE(parsed.args[1], std::string("/foo bar/movie.mp4"));
    }

    void TokenizeReportsBadQuotes()
    {
        const auto parsed = CommandRegistry::Tokenize("open-file \"unterminated");

        QVERIFY(!(parsed.error.empty()));
        QVERIFY(parsed.args.empty());
    }

    void ParseDurationUnits()
    {
        double seconds = 0.0;
        QVERIFY(CommandRegistry::ParseDurationSeconds("30s", seconds));
        QCOMPARE(seconds, 30.0);
        QVERIFY(CommandRegistry::ParseDurationSeconds("-500ms", seconds));
        QCOMPARE(seconds, -0.5);
        QVERIFY(CommandRegistry::ParseDurationSeconds("2m", seconds));
        QCOMPARE(seconds, 120.0);
        QVERIFY(!(CommandRegistry::ParseDurationSeconds("nope", seconds)));
    }

    void RejectsDuplicateNamesAndAliases()
    {
        CommandRegistry registry;
        ICommandRegistry::CommandSpec first;
        first.name = "echo";
        first.aliases = "say";
        first.handler = &EchoHandler;
        QVERIFY(registry.RegisterCommand(&first));

        ICommandRegistry::CommandSpec duplicate;
        duplicate.name = "say";
        duplicate.handler = &EchoHandler;
        QVERIFY(!(registry.RegisterCommand(&duplicate)));
    }

    void ExecuteDispatchesAndCapturesOutput()
    {
        CommandRegistry registry;
        ICommandRegistry::CommandSpec echo;
        echo.name = "echo";
        echo.aliases = "say";
        echo.handler = &EchoHandler;
        QVERIFY(registry.RegisterCommand(&echo));

        OutputCapture capture;
        QVERIFY(registry.Execute("say hello", &CaptureOutput, &capture));

        QCOMPARE(capture.lines.size(), 2u);
        QCOMPARE(capture.lines[0], std::string("say"));
        QCOMPARE(capture.lines[1], std::string("hello"));
    }

    void UnknownCommandEmitsDiagnostic()
    {
        CommandRegistry registry;
        OutputCapture capture;

        QVERIFY(!(registry.Execute("missing", &CaptureOutput, &capture)));

        QCOMPARE(capture.levels.front(), static_cast<int>(ICommandRegistry::OutputError));
        QVERIFY(capture.lines.front().starts_with("Unknown command"));
    }
};

namespace
{
const ::framelift::test::Registrar<CommandRegistryTest> kRegisterCommandRegistryTest{"CommandRegistryTest"};
}

#include "CommandRegistryTests.moc"
