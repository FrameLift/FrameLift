#include "HotkeysImpl.h"

#include "QtTestRunner.h"
#include <framelift/AppEvent.h>

#include <QtTest/QtTest>

// Exercises the host Hotkeys implementation directly — in particular the multi-key
// grouping that the Settings → Keybinds editor relies on: every key bound to an
// action shares that action's name, so Unbind/Rebind/RebindList act on the whole
// group instead of leaving orphaned aliases behind.

namespace
{
AppEvent KeyEvent(Key key, Mod mods)
{
    AppEvent e;
    e.type = AppEventType::KeyDown;
    e.AsKey() = {key, mods};
    return e;
}

// Doubles as both the action user-data and the cleanup user-data.
struct Tracked
{
    int fired = 0;
    int freed = 0;
};

void Fire(void* ud)
{
    static_cast<Tracked*>(ud)->fired++;
}

void Free(void* ud)
{
    static_cast<Tracked*>(ud)->freed++;
}
} // namespace

class HotkeysTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void MultipleKeysFireSameAction()
    {
        HotkeysImpl hk;
        Tracked t;
        hk.BindNamedRaw("act", "Ctrl+F;F2", &Fire, &t, nullptr);

        QVERIFY(hk.Handle(KeyEvent(Keys::F, Mod::Ctrl)));
        QVERIFY(hk.Handle(KeyEvent(Keys::F2, Mod::None)));
        QVERIFY((t.fired) == (2));
        // A non-bound key is ignored.
        QVERIFY(!(hk.Handle(KeyEvent(Keys::G, Mod::None))));
        QVERIFY((t.fired) == (2));
    }

    void GetShortcutStringReturnsPrimaryKey()
    {
        HotkeysImpl hk;
        Tracked t;
        hk.BindNamedRaw("act", "Ctrl+F;F2", &Fire, &t, nullptr);

        char buf[64] = {};
        hk.GetShortcutString("act", buf, sizeof(buf));
        QVERIFY(::framelift::test::CStringEqual(buf, "Ctrl+F")); // only the primary, for menu/overlay labels
    }

    void RebindListSwapsTheWholeKeySet()
    {
        HotkeysImpl hk;
        Tracked t;
        hk.BindNamedRaw("act", "Ctrl+F;F2", &Fire, &t, nullptr);

        hk.RebindList("act", "G;H");

        QVERIFY(!(hk.Handle(KeyEvent(Keys::F, Mod::Ctrl)))); // old keys gone
        QVERIFY(!(hk.Handle(KeyEvent(Keys::F2, Mod::None))));
        QVERIFY(hk.Handle(KeyEvent(Keys::G, Mod::None)));
        QVERIFY(hk.Handle(KeyEvent(Keys::H, Mod::None)));
        QVERIFY((t.fired) == (2));
    }

    void RebindListReusesCallbackAndFreesOnce()
    {
        HotkeysImpl hk;
        Tracked t;
        hk.BindNamedRaw("act", "A;B", &Fire, &t, &Free);

        hk.RebindList("act", "C;D;E");
        QVERIFY((t.freed) == (0)); // swapping keys must not free the reused callback

        QVERIFY(hk.Handle(KeyEvent(Keys::C, Mod::None)));
        QVERIFY(hk.Handle(KeyEvent(Keys::D, Mod::None)));
        QVERIFY(hk.Handle(KeyEvent(Keys::E, Mod::None)));
        QVERIFY((t.fired) == (3));

        hk.Clear();
        QVERIFY((t.freed) == (1)); // exactly once — only the primary owns the cleanup
    }

    void BindNamedRawFreesCallbackWhenNothingToBind()
    {
        HotkeysImpl hk;
        Tracked t;
        // An empty (or all-invalid) bind list binds no keys; the callback whose
        // ownership we handed to BindNamedRaw must be freed here, not leaked.
        hk.BindNamedRaw("act", "", &Fire, &t, &Free);
        QVERIFY((t.freed) == (1));
        QVERIFY(!(hk.Handle(KeyEvent(Keys::A, Mod::None))));
        QVERIFY((t.fired) == (0));
    }

    void RebindListOnUnknownNameIsNoOp()
    {
        HotkeysImpl hk;
        hk.RebindList("nope", "A");
        QVERIFY(!(hk.Handle(KeyEvent(Keys::A, Mod::None))));
    }

    void UnbindRemovesEveryKeyOfTheAction()
    {
        HotkeysImpl hk;
        Tracked t;
        hk.BindNamedRaw("act", "A;B", &Fire, &t, nullptr);

        hk.Unbind("act");

        QVERIFY(!(hk.Handle(KeyEvent(Keys::A, Mod::None)))); // no ghost alias still firing
        QVERIFY(!(hk.Handle(KeyEvent(Keys::B, Mod::None))));
        QVERIFY((t.fired) == (0));
    }

    void RebindCollapsesMultiKeyActionToSingleKey()
    {
        HotkeysImpl hk;
        Tracked t;
        hk.BindNamedRaw("act", "A;B", &Fire, &t, nullptr);

        QVERIFY(hk.Rebind("act", Keys::C, Mod::None));

        QVERIFY(hk.Handle(KeyEvent(Keys::C, Mod::None)));    // new primary
        QVERIFY(!(hk.Handle(KeyEvent(Keys::A, Mod::None)))); // old primary replaced
        QVERIFY(!(hk.Handle(KeyEvent(Keys::B, Mod::None)))); // alias dropped
        QVERIFY((t.fired) == (1));
    }

    void RebindingNameReplacesOldGroupAndFreesIt()
    {
        HotkeysImpl hk;
        Tracked first;
        hk.BindNamedRaw("act", "A;B", &Fire, &first, &Free);

        Tracked second;
        hk.BindNamedRaw("act", "C;D", &Fire, &second, &Free);

        QVERIFY((first.freed) == (1)); // old group's callback freed on replace

        QVERIFY(!(hk.Handle(KeyEvent(Keys::A, Mod::None)))); // old keys gone, no accumulation
        QVERIFY(!(hk.Handle(KeyEvent(Keys::B, Mod::None))));
        QVERIFY(hk.Handle(KeyEvent(Keys::C, Mod::None)));
        QVERIFY(hk.Handle(KeyEvent(Keys::D, Mod::None)));
        QVERIFY((second.fired) == (2));
    }
};

namespace
{
const ::framelift::test::Registrar<HotkeysTest> kRegisterHotkeysTest{"HotkeysTest"};
}

#include "HotkeysTests.moc"
