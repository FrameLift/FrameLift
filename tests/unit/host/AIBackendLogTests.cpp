#include "AIBackendLog.h"
#include "QtTestRunner.h"

#include <QtTest/QtTest>

class AIBackendLogTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void GateAcceptsOnlyOne()
    {
        QVERIFY(!hostai::AIBackendLoggingEnabled(nullptr));
        QVERIFY(!hostai::AIBackendLoggingEnabled(""));
        QVERIFY(!hostai::AIBackendLoggingEnabled("0"));
        QVERIFY(!hostai::AIBackendLoggingEnabled("true"));
        QVERIFY(hostai::AIBackendLoggingEnabled("1"));
    }

    void JoinsContinuationFragments()
    {
        hostai::AIBackendLogBuffer buffer;
        QVERIFY(buffer.Push(hostai::AIBackendLogLevel::Info, "loading ").empty());
        const auto lines = buffer.Push(hostai::AIBackendLogLevel::Continue, "model\r\n");
        QCOMPARE(static_cast<int>(lines.size()), 1);
        QVERIFY(lines[0].level == hostai::AIBackendLogLevel::Info);
        QCOMPARE(QString::fromStdString(lines[0].text), QStringLiteral("loading model"));
    }

    void PreservesSeverityAndSuppressesEmptyLines()
    {
        hostai::AIBackendLogBuffer buffer;
        auto lines = buffer.Push(hostai::AIBackendLogLevel::Warn, "warning\n\n");
        QCOMPARE(static_cast<int>(lines.size()), 1);
        QVERIFY(lines[0].level == hostai::AIBackendLogLevel::Warn);
        QCOMPARE(QString::fromStdString(lines[0].text), QStringLiteral("warning"));

        QVERIFY(buffer.Push(hostai::AIBackendLogLevel::Error, "partial error").empty());
        lines = buffer.Push(hostai::AIBackendLogLevel::Debug, "next\n");
        QCOMPARE(static_cast<int>(lines.size()), 2);
        QVERIFY(lines[0].level == hostai::AIBackendLogLevel::Error);
        QCOMPARE(QString::fromStdString(lines[0].text), QStringLiteral("partial error"));
        QVERIFY(lines[1].level == hostai::AIBackendLogLevel::Debug);
        QCOMPARE(QString::fromStdString(lines[1].text), QStringLiteral("next"));
    }
};

namespace
{
const ::framelift::test::Registrar<AIBackendLogTest> kRegisterAIBackendLogTest{"AIBackendLogTest"};
}

#include "AIBackendLogTests.moc"
