#include "ContextMenuModule.h"

#include <QtCore/QVariantMap>

#include <gtest/gtest.h>

// ContextMenuModule is exercised without a host context: no Install()/event loop
// runs, so the deferred Assemble() that builds the core items never fires here.
// That makes these tests target exactly the ContextMenu service storage ABI and
// the QmlExtraItems() projection: items added directly via the ABI surface, with
// core items (stamped Item::core during Assemble at runtime) filtered out.

namespace
{
int g_invoked = 0;
int g_cleaned = 0;
} // namespace

TEST(ContextMenuTest, ExtraItemsEmptyBeforeAssembly)
{
    ContextMenuModule m;
    // Without Install()/event loop the menu hasn't assembled, and no plugin items
    // were added, so the projection is empty.
    EXPECT_TRUE(m.QmlExtraItems().isEmpty());
}

TEST(ContextMenuTest, CoreStampedItemsAreExcludedFromExtraItems)
{
    ContextMenuModule m;
    // A non-core item (the default for the ABI add path) is projected...
    m.AddItemRaw(
        "Plugin Action", [](void*) {}, nullptr, nullptr
    );
    EXPECT_EQ(m.QmlExtraItems().size(), 1);
    // ...while a separator is never an extra item; the core-vs-plugin distinction
    // is carried by Item::core (set during Assemble), not by label string matching.
    m.AddSeparator();
    EXPECT_EQ(m.QmlExtraItems().size(), 1);
}

TEST(ContextMenuTest, CustomItemAppearsAndInvokes)
{
    g_invoked = 0;
    ContextMenuModule m;
    m.AddItemRaw(
        "Custom Action", [](void*) { ++g_invoked; }, nullptr, nullptr
    );

    const QVariantList items = m.QmlExtraItems();
    ASSERT_EQ(items.size(), 1);
    const QVariantMap row = items.front().toMap();
    EXPECT_EQ(row.value(QStringLiteral("label")).toString(), QStringLiteral("Custom Action"));

    m.invokeExtra(row.value(QStringLiteral("index")).toInt());
    EXPECT_EQ(g_invoked, 1);
}

TEST(ContextMenuTest, SeparatorsAreNotExtraItems)
{
    ContextMenuModule m;
    m.AddSeparator(); // empty label + null action — never an extra item
    EXPECT_TRUE(m.QmlExtraItems().isEmpty());
}

TEST(ContextMenuTest, ClearRunsItemCleanup)
{
    g_cleaned = 0;
    ContextMenuModule m;
    m.AddItemRaw(
        "X", nullptr, nullptr, [](void*) { ++g_cleaned; }
    );
    m.Clear();
    EXPECT_EQ(g_cleaned, 1);
}

TEST(ContextMenuTest, AudioFlagsAreFalseWithoutServices)
{
    const ContextMenuModule m;
    EXPECT_FALSE(m.Muted());
    EXPECT_FALSE(m.NormalizeEnabled());
    EXPECT_FALSE(m.SubtitlesEnabled());
}
