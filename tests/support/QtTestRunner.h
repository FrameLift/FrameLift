#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <cmath>
#include <cstring>
#include <functional>
#include <vector>

namespace framelift::test
{
struct SuiteEntry
{
    QString name;
    std::function<QObject*()> factory;
};

class Registry
{
public:
    static Registry& Instance();

    void Add(QString name, std::function<QObject*()> factory);
    [[nodiscard]] const std::vector<SuiteEntry>& Suites() const noexcept;

private:
    std::vector<SuiteEntry> suites_;
};

template <typename T>
class Registrar
{
public:
    explicit Registrar(const char* name)
    {
        Registry::Instance().Add(
            QString::fromLatin1(name),
            []
            {
                return new T;
            }
        );
    }
};

int RunRegisteredQtTests(int argc, char** argv);

inline bool CStringEqual(const char* actual, const char* expected) noexcept
{
    if (!actual || !expected)
    {
        return actual == expected;
    }
    return std::strcmp(actual, expected) == 0;
}

inline bool CStringNotEqual(const char* actual, const char* expected) noexcept
{
    return !CStringEqual(actual, expected);
}

template <typename A, typename B, typename Epsilon>
bool NearlyEqual(A actual, B expected, Epsilon epsilon)
{
    using std::abs;
    return abs(actual - expected) <= epsilon;
}
} // namespace framelift::test
