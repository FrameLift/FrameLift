#include "HotkeysImpl.h"

#include <framelift/AppEvent.h>
#include <gtest/gtest.h>

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

TEST(HotkeysTest, MultipleKeysFireSameAction)
{
    HotkeysImpl hk;
    Tracked t;
    hk.BindNamedRaw("act", "Ctrl+F;F2", &Fire, &t, nullptr);

    EXPECT_TRUE(hk.Handle(KeyEvent(Keys::F, Mod::Ctrl)));
    EXPECT_TRUE(hk.Handle(KeyEvent(Keys::F2, Mod::None)));
    EXPECT_EQ(t.fired, 2);
    // A non-bound key is ignored.
    EXPECT_FALSE(hk.Handle(KeyEvent(Keys::G, Mod::None)));
    EXPECT_EQ(t.fired, 2);
}

TEST(HotkeysTest, GetShortcutStringReturnsPrimaryKey)
{
    HotkeysImpl hk;
    Tracked t;
    hk.BindNamedRaw("act", "Ctrl+F;F2", &Fire, &t, nullptr);

    char buf[64] = {};
    hk.GetShortcutString("act", buf, sizeof(buf));
    EXPECT_STREQ(buf, "Ctrl+F"); // only the primary, for menu/overlay labels
}

TEST(HotkeysTest, RebindListSwapsTheWholeKeySet)
{
    HotkeysImpl hk;
    Tracked t;
    hk.BindNamedRaw("act", "Ctrl+F;F2", &Fire, &t, nullptr);

    hk.RebindList("act", "G;H");

    EXPECT_FALSE(hk.Handle(KeyEvent(Keys::F, Mod::Ctrl))); // old keys gone
    EXPECT_FALSE(hk.Handle(KeyEvent(Keys::F2, Mod::None)));
    EXPECT_TRUE(hk.Handle(KeyEvent(Keys::G, Mod::None)));
    EXPECT_TRUE(hk.Handle(KeyEvent(Keys::H, Mod::None)));
    EXPECT_EQ(t.fired, 2);
}

TEST(HotkeysTest, RebindListReusesCallbackAndFreesOnce)
{
    HotkeysImpl hk;
    Tracked t;
    hk.BindNamedRaw("act", "A;B", &Fire, &t, &Free);

    hk.RebindList("act", "C;D;E");
    EXPECT_EQ(t.freed, 0); // swapping keys must not free the reused callback

    EXPECT_TRUE(hk.Handle(KeyEvent(Keys::C, Mod::None)));
    EXPECT_TRUE(hk.Handle(KeyEvent(Keys::D, Mod::None)));
    EXPECT_TRUE(hk.Handle(KeyEvent(Keys::E, Mod::None)));
    EXPECT_EQ(t.fired, 3);

    hk.Clear();
    EXPECT_EQ(t.freed, 1); // exactly once — only the primary owns the cleanup
}

TEST(HotkeysTest, RebindListOnUnknownNameIsNoOp)
{
    HotkeysImpl hk;
    hk.RebindList("nope", "A");
    EXPECT_FALSE(hk.Handle(KeyEvent(Keys::A, Mod::None)));
}

TEST(HotkeysTest, UnbindRemovesEveryKeyOfTheAction)
{
    HotkeysImpl hk;
    Tracked t;
    hk.BindNamedRaw("act", "A;B", &Fire, &t, nullptr);

    hk.Unbind("act");

    EXPECT_FALSE(hk.Handle(KeyEvent(Keys::A, Mod::None))); // no ghost alias still firing
    EXPECT_FALSE(hk.Handle(KeyEvent(Keys::B, Mod::None)));
    EXPECT_EQ(t.fired, 0);
}

TEST(HotkeysTest, RebindCollapsesMultiKeyActionToSingleKey)
{
    HotkeysImpl hk;
    Tracked t;
    hk.BindNamedRaw("act", "A;B", &Fire, &t, nullptr);

    EXPECT_TRUE(hk.Rebind("act", Keys::C, Mod::None));

    EXPECT_TRUE(hk.Handle(KeyEvent(Keys::C, Mod::None)));  // new primary
    EXPECT_FALSE(hk.Handle(KeyEvent(Keys::A, Mod::None))); // old primary replaced
    EXPECT_FALSE(hk.Handle(KeyEvent(Keys::B, Mod::None))); // alias dropped
    EXPECT_EQ(t.fired, 1);
}

TEST(HotkeysTest, RebindingNameReplacesOldGroupAndFreesIt)
{
    HotkeysImpl hk;
    Tracked first;
    hk.BindNamedRaw("act", "A;B", &Fire, &first, &Free);

    Tracked second;
    hk.BindNamedRaw("act", "C;D", &Fire, &second, &Free);

    EXPECT_EQ(first.freed, 1); // old group's callback freed on replace

    EXPECT_FALSE(hk.Handle(KeyEvent(Keys::A, Mod::None))); // old keys gone, no accumulation
    EXPECT_FALSE(hk.Handle(KeyEvent(Keys::B, Mod::None)));
    EXPECT_TRUE(hk.Handle(KeyEvent(Keys::C, Mod::None)));
    EXPECT_TRUE(hk.Handle(KeyEvent(Keys::D, Mod::None)));
    EXPECT_EQ(second.fired, 2);
}
