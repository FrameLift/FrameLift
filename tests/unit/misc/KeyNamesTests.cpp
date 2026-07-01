#include <framelift/Hotkeys.h>

#include "QtTestRunner.h"
#include <cstring>

#include <QtTest/QtTest>

namespace
{
std::string Bind(const Key key, const Mod mods, const int cap = 64)
{
    char buf[128] = {};
    KeyBindToString(key, mods, buf, cap);
    return buf;
}
} // namespace

class KeyNamesTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void PlainLetter()
    {
        QVERIFY((Bind(Keys::F, Mod::None)) == ("F"));
    }

    void SingleModifier()
    {
        QVERIFY((Bind(Keys::F, Mod::Ctrl)) == ("Ctrl+F"));
    }

    void MultipleModifiersInCanonicalOrder()
    {
        QVERIFY((Bind(Keys::F, Mod::Ctrl | Mod::Shift)) == ("Ctrl+Shift+F"));
        QVERIFY((Bind(Keys::F, Mod::Ctrl | Mod::Shift | Mod::Alt)) == ("Ctrl+Shift+Alt+F"));
        // Order is normalised regardless of how the flags were combined.
        QVERIFY((Bind(Keys::F, Mod::Alt | Mod::Ctrl)) == ("Ctrl+Alt+F"));
    }

    void NamedKeys()
    {
        QVERIFY((Bind(Keys::Escape, Mod::None)) == ("Escape"));
        QVERIFY((Bind(Keys::Space, Mod::None)) == ("Space"));
        QVERIFY((Bind(Keys::Num0, Mod::None)) == ("0"));
    }

    void UnknownKeyIsQuestionMark()
    {
        QVERIFY((Bind(0x7FFFFFFFu, Mod::None)) == ("?"));
    }

    void TruncatesToBufferCapacity()
    {
        char buf[4] = {};
        KeyBindToString(Keys::F, Mod::Ctrl, buf, 4); // "Ctrl+F" -> fits 3 chars + NUL
        QVERIFY((std::strlen(buf)) == (3u));
        QVERIFY(::framelift::test::CStringEqual(buf, "Ctr"));
    }

    void RejectsZeroCapacity()
    {
        char buf[8] = {'x', 'x', 'x', 0};
        const char* r = KeyBindToString(Keys::F, Mod::None, buf, 0);
        QVERIFY(::framelift::test::CStringEqual(r, "")); // returns empty string, leaves buf untouched
    }
};

namespace
{
const ::framelift::test::Registrar<KeyNamesTest> kRegisterKeyNamesTest{"KeyNamesTest"};
}

#include "KeyNamesTests.moc"
