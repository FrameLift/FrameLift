#include "QtTestRunner.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QMetaMethod>
#include <QtCore/QStringList>
#include <QtTest/QTest>
#include <iostream>
#include <memory>

namespace framelift::test
{
Registry& Registry::Instance()
{
    static Registry registry;
    return registry;
}

void Registry::Add(QString name, std::function<QObject*()> factory)
{
    suites_.push_back(SuiteEntry{std::move(name), std::move(factory)});
}

const std::vector<SuiteEntry>& Registry::Suites() const noexcept
{
    return suites_;
}

namespace
{
QStringList CaseNames(const QObject& suite)
{
    QStringList cases;
    const QMetaObject* meta = suite.metaObject();
    for (int i = meta->methodOffset(); i < meta->methodCount(); ++i)
    {
        const QMetaMethod method = meta->method(i);
        if (method.methodType() == QMetaMethod::Slot && method.parameterCount() == 0)
        {
            cases.push_back(QString::fromLatin1(method.name()));
        }
    }
    cases.sort();
    return cases;
}

int RunSuite(const SuiteEntry& entry, const QString* caseName, int argc, char** argv)
{
    std::unique_ptr<QObject> suite(entry.factory());
    if (!caseName)
    {
        return QTest::qExec(suite.get(), argc, argv);
    }

    QByteArray caseNameBytes = caseName->toLatin1();
    char* qtArgv[] = {argv[0], caseNameBytes.data()};
    return QTest::qExec(suite.get(), 2, qtArgv);
}
} // namespace

int RunRegisteredQtTests(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const auto& suites = Registry::Instance().Suites();

    if (argc == 2 && QString::fromLatin1(argv[1]) == QStringLiteral("--framelift-list-tests"))
    {
        for (const SuiteEntry& entry : suites)
        {
            std::unique_ptr<QObject> suite(entry.factory());
            for (const QString& testCase : CaseNames(*suite))
            {
                std::cout << entry.name.toStdString() << '.' << testCase.toStdString() << '\n';
            }
        }
        return 0;
    }

    if (argc == 3 && QString::fromLatin1(argv[1]) == QStringLiteral("--framelift-test"))
    {
        const QString filter = QString::fromLatin1(argv[2]);
        const int dot = filter.indexOf(QLatin1Char('.'));
        if (dot <= 0 || dot == filter.size() - 1)
        {
            std::cerr << "Expected --framelift-test Suite.Case\n";
            return 2;
        }

        const QString suiteName = filter.left(dot);
        const QString caseName = filter.mid(dot + 1);
        for (const SuiteEntry& entry : suites)
        {
            if (entry.name == suiteName)
            {
                return RunSuite(entry, &caseName, argc, argv);
            }
        }

        std::cerr << "Unknown test suite: " << suiteName.toStdString() << '\n';
        return 2;
    }

    int result = 0;
    for (const SuiteEntry& entry : suites)
    {
        result |= RunSuite(entry, nullptr, argc, argv);
    }
    return result;
}
} // namespace framelift::test

int main(int argc, char** argv)
{
    return framelift::test::RunRegisteredQtTests(argc, argv);
}
