#include "SemVer.h"

#include "QtTestRunner.h"

#include <QtTest/QtTest>

class SemVerTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void ParsesFullVersion()
    {
        const SemVer v = ParseVersion("1.2.3");
        QVERIFY((v.major) == (1));
        QVERIFY((v.minor) == (2));
        QVERIFY((v.patch) == (3));
    }

    void ParsesMultiDigitComponents()
    {
        const SemVer v = ParseVersion("2.10.5");
        QVERIFY((v.major) == (2));
        QVERIFY((v.minor) == (10));
        QVERIFY((v.patch) == (5));
    }

    void MissingComponentsDefaultToZero()
    {
        QVERIFY((ParseVersion("1").minor) == (0));
        QVERIFY((ParseVersion("1").patch) == (0));
        QVERIFY((ParseVersion("1.2").patch) == (0));
    }

    void MalformedParsesToZero()
    {
        const SemVer v = ParseVersion("not-a-version");
        QVERIFY((v.major) == (0));
        QVERIFY((v.minor) == (0));
        QVERIFY((v.patch) == (0));
    }

    void OrdersByMajorThenMinorThenPatch()
    {
        QVERIFY(ParseVersion("1.0.1") > ParseVersion("1.0.0"));
        QVERIFY(ParseVersion("1.1.0") > ParseVersion("1.0.9"));
        QVERIFY(ParseVersion("2.0.0") > ParseVersion("1.9.9"));
    }

    void EqualIsNotGreater()
    {
        QVERIFY(!(ParseVersion("1.2.3") > ParseVersion("1.2.3")));
        QVERIFY(!(ParseVersion("1.0.0") > ParseVersion("1.0.1")));
    }
};

namespace
{
const ::framelift::test::Registrar<SemVerTest> kRegisterSemVerTest{"SemVerTest"};
}

#include "SemVerTests.moc"
