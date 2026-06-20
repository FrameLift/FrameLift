#include <framelift/Hotkeys.h>

#include <cstring>
#include <gtest/gtest.h>

namespace
{
std::string Bind(const Key key, const Mod mods, const int cap = 64)
{
    char buf[128] = {};
    KeyBindToString(key, mods, buf, cap);
    return buf;
}
} // namespace

TEST(KeyNamesTest, PlainLetter)
{
    EXPECT_EQ(Bind(Keys::F, Mod::None), "F");
}

TEST(KeyNamesTest, SingleModifier)
{
    EXPECT_EQ(Bind(Keys::F, Mod::Ctrl), "Ctrl+F");
}

TEST(KeyNamesTest, MultipleModifiersInCanonicalOrder)
{
    EXPECT_EQ(Bind(Keys::F, Mod::Ctrl | Mod::Shift), "Ctrl+Shift+F");
    EXPECT_EQ(Bind(Keys::F, Mod::Ctrl | Mod::Shift | Mod::Alt), "Ctrl+Shift+Alt+F");
    // Order is normalised regardless of how the flags were combined.
    EXPECT_EQ(Bind(Keys::F, Mod::Alt | Mod::Ctrl), "Ctrl+Alt+F");
}

TEST(KeyNamesTest, NamedKeys)
{
    EXPECT_EQ(Bind(Keys::Escape, Mod::None), "Escape");
    EXPECT_EQ(Bind(Keys::Space, Mod::None), "Space");
    EXPECT_EQ(Bind(Keys::Num0, Mod::None), "0");
}

TEST(KeyNamesTest, UnknownKeyIsQuestionMark)
{
    EXPECT_EQ(Bind(0x7FFFFFFFu, Mod::None), "?");
}

TEST(KeyNamesTest, TruncatesToBufferCapacity)
{
    char buf[4] = {};
    KeyBindToString(Keys::F, Mod::Ctrl, buf, 4); // "Ctrl+F" -> fits 3 chars + NUL
    EXPECT_EQ(std::strlen(buf), 3u);
    EXPECT_STREQ(buf, "Ctr");
}

TEST(KeyNamesTest, RejectsZeroCapacity)
{
    char buf[8] = {'x', 'x', 'x', 0};
    const char* r = KeyBindToString(Keys::F, Mod::None, buf, 0);
    EXPECT_STREQ(r, ""); // returns empty string, leaves buf untouched
}
