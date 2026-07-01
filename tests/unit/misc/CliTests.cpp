#include "Cli.h"

#include "QtTestRunner.h"

#include <QtTest/QtTest>

namespace
{
// argv arrays are NUL-terminated const char* lists, with argv[0] = program name.
std::string Parse(const std::initializer_list<const char*> args)
{
    return ParseOpenTarget(static_cast<int>(args.size()), args.begin());
}
} // namespace

class CliTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void ReturnsLocalPath()
    {
        QVERIFY((Parse({"FrameLift", "movie.mkv"})) == ("movie.mkv"));
    }

    void ReturnsUrl()
    {
        QVERIFY((Parse({"FrameLift", "https://example.com/stream.m3u8"})) == ("https://example.com/stream.m3u8"));
    }

    void EmptyWhenNoArgs()
    {
        QVERIFY((Parse({"FrameLift"})) == (""));
    }

    void SkipsLeadingFlags()
    {
        QVERIFY((Parse({"FrameLift", "--verbose", "-x", "movie.mkv"})) == ("movie.mkv"));
    }

    void EmptyWhenOnlyFlags()
    {
        QVERIFY((Parse({"FrameLift", "--help", "-v"})) == (""));
    }

    void FirstPositionalWinsOverLaterArgs()
    {
        QVERIFY((Parse({"FrameLift", "first.mkv", "second.mkv"})) == ("first.mkv"));
    }

    void HandlesNullArgvAndZeroArgc()
    {
        QVERIFY((ParseOpenTarget(0, nullptr)) == (""));
        QVERIFY((ParseOpenTarget(5, nullptr)) == (""));
    }
};

namespace
{
const ::framelift::test::Registrar<CliTest> kRegisterCliTest{"CliTest"};
}

#include "CliTests.moc"
