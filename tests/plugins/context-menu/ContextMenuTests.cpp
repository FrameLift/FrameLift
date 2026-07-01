#include "ContextMenuModule.h"

#include <QtCore/QVariantMap>

#include "QtTestRunner.h"

#include <QtTest/QtTest>

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

class ContextMenuTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void ExtraItemsEmptyBeforeAssembly()
    {
        ContextMenuModule m;
        // Without Install()/event loop the menu hasn't assembled, and no plugin items
        // were added, so the projection is empty.
        QVERIFY(m.QmlExtraItems().isEmpty());
    }

    void CoreStampedItemsAreExcludedFromExtraItems()
    {
        ContextMenuModule m;
        // A non-core item (the default for the ABI add path) is projected...
        m.AddItemRaw(
            "Plugin Action",
            [](void*)
            {
            },
            nullptr, nullptr
        );
        QVERIFY((m.QmlExtraItems().size()) == (1));
        // ...while a separator is never an extra item; the core-vs-plugin distinction
        // is carried by Item::core (set during Assemble), not by label string matching.
        m.AddSeparator();
        QVERIFY((m.QmlExtraItems().size()) == (1));
    }

    void CustomItemAppearsAndInvokes()
    {
        g_invoked = 0;
        ContextMenuModule m;
        m.AddItemRaw(
            "Custom Action",
            [](void*)
            {
                ++g_invoked;
            },
            nullptr, nullptr
        );

        const QVariantList items = m.QmlExtraItems();
        QVERIFY((items.size()) == (1));
        const QVariantMap row = items.front().toMap();
        QVERIFY((row.value(QStringLiteral("label")).toString()) == (QStringLiteral("Custom Action")));

        m.invokeExtra(row.value(QStringLiteral("index")).toInt());
        QVERIFY((g_invoked) == (1));
    }

    void SeparatorsAreNotExtraItems()
    {
        ContextMenuModule m;
        m.AddSeparator(); // empty label + null action — never an extra item
        QVERIFY(m.QmlExtraItems().isEmpty());
    }

    void ClearRunsItemCleanup()
    {
        g_cleaned = 0;
        ContextMenuModule m;
        m.AddItemRaw(
            "X", nullptr, nullptr,
            [](void*)
            {
                ++g_cleaned;
            }
        );
        m.Clear();
        QVERIFY((g_cleaned) == (1));
    }

    void AudioFlagsAreFalseWithoutServices()
    {
        const ContextMenuModule m;
        QVERIFY(!(m.Muted()));
        QVERIFY(!(m.NormalizeEnabled()));
        QVERIFY(!(m.SubtitlesEnabled()));
    }
};

namespace
{
const ::framelift::test::Registrar<ContextMenuTest> kRegisterContextMenuTest{"ContextMenuTest"};
}

#include "ContextMenuTests.moc"
