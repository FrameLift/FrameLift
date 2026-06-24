#include "FocusManagerImpl.h"

#include <gtest/gtest.h>
#include <framelift/IModule.h>

// FocusManagerImpl is a header-only stack of IModule*; the tests only need
// distinct addresses, so trivial IModule instances suffice.

namespace
{
class DummyModule final : public IModule
{
};
} // namespace

TEST(FocusManagerTest, EmptyHasNoFocus)
{
    const FocusManagerImpl fm;
    EXPECT_EQ(fm.Focused(), nullptr);
}

TEST(FocusManagerTest, AcquireSetsFocus)
{
    FocusManagerImpl fm;
    DummyModule a;
    fm.Acquire(&a);
    EXPECT_EQ(fm.Focused(), &a);
}

TEST(FocusManagerTest, DoubleAcquireKeepsSingleEntry)
{
    FocusManagerImpl fm;
    DummyModule a;
    fm.Acquire(&a);
    fm.Acquire(&a);
    EXPECT_EQ(fm.Focused(), &a);
    fm.Release(&a);
    EXPECT_EQ(fm.Focused(), nullptr); // a single Release clears it - no duplicate left behind
}

TEST(FocusManagerTest, ReacquireMovesToTop)
{
    FocusManagerImpl fm;
    DummyModule a, b;
    fm.Acquire(&a);
    fm.Acquire(&b);
    EXPECT_EQ(fm.Focused(), &b);
    fm.Acquire(&a); // re-acquire moves a above b
    EXPECT_EQ(fm.Focused(), &a);
}

TEST(FocusManagerTest, ReleaseTopRevealsPrevious)
{
    FocusManagerImpl fm;
    DummyModule a, b;
    fm.Acquire(&a);
    fm.Acquire(&b);
    fm.Release(&b);
    EXPECT_EQ(fm.Focused(), &a);
}

TEST(FocusManagerTest, ReleaseNonAcquiredIsNoOp)
{
    FocusManagerImpl fm;
    DummyModule a, b;
    fm.Acquire(&a);
    fm.Release(&b);
    EXPECT_EQ(fm.Focused(), &a);
}

TEST(FocusManagerTest, ReleaseMiddleKeepsTop)
{
    FocusManagerImpl fm;
    DummyModule a, b, c;
    fm.Acquire(&a);
    fm.Acquire(&b);
    fm.Acquire(&c);
    fm.Release(&b);
    EXPECT_EQ(fm.Focused(), &c);
    fm.Release(&c);
    EXPECT_EQ(fm.Focused(), &a);
}
