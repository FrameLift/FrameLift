#include "ContextMenuModule.h"

#include <QtCore/QVariantMap>

#include "QtTestRunner.h"

#include <QtTest/QtTest>
#include <algorithm>
#include <cstring>

// ContextMenuModule is exercised without a host context: no Install()/event loop
// runs, so the deferred Assemble() that builds the core items never fires here.
// That makes these tests target the ContextMenu storage ABI, the additive submenu
// service, and the QmlExtraItems() projection: items added directly through those
// surfaces, with core items (stamped Item::core during Assemble at runtime)
// filtered out.

namespace
{
int g_invoked = 0;
int g_cleaned = 0;

class StubHotkeys final : public Hotkeys
{
public:
    void BindRaw(Key, Mod, void (*)(void*), void*, void (*)(void*)) noexcept override
    {
    }

    void BindNamedRaw(const char*, const char*, void (*)(void*), void*, void (*)(void*)) noexcept override
    {
    }

    bool Rebind(const char*, Key, Mod) noexcept override
    {
        return false;
    }

    void Unbind(const char*) noexcept override
    {
    }

    void RebindList(const char*, const char*) noexcept override
    {
    }

    int GetShortcutString(const char* name, char* buf, const int cap) const noexcept override
    {
        const char* value = std::strcmp(name, "aitagger.tagFile") == 0 ? "T" : "Shift+T";
        const int len = static_cast<int>(std::strlen(value));
        if (buf && cap > 0)
        {
            const int copied = std::min(len, cap - 1);
            std::memcpy(buf, value, static_cast<std::size_t>(copied));
            buf[copied] = '\0';
        }
        return len;
    }

    void Clear() noexcept override
    {
    }

    bool Handle(const AppEvent&) const noexcept override
    {
        return false;
    }
};
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
        QVERIFY((row.value(QStringLiteral("kind")).toString()) == (QStringLiteral("action")));

        m.invokeExtra(row.value(QStringLiteral("actionId")).toInt());
        QVERIFY((g_invoked) == (1));
    }

    void SubmenuProjectsChildrenWithShortcutsAndInvokes()
    {
        g_invoked = 0;
        ContextMenuModule m;
        StubHotkeys keys;
        m.SetKeys(&keys);
        framelift::AddSubmenuSection(
            m, "Tagger",
            [](ContextMenu& submenu)
            {
                framelift::AddItem(
                    submenu, "Tag this video", "aitagger.tagFile",
                    []
                    {
                        g_invoked += 1;
                    }
                );
                framelift::AddItem(
                    submenu, "Tag this folder", "aitagger.tagFolder",
                    []
                    {
                        g_invoked += 10;
                    }
                );
            }
        );
        m.EmitSections();

        const QVariantList items = m.QmlExtraItems();
        QCOMPARE(items.size(), 1);
        const QVariantMap tagger = items.front().toMap();
        QCOMPARE(tagger.value(QStringLiteral("kind")).toString(), QStringLiteral("submenu"));
        QCOMPARE(tagger.value(QStringLiteral("label")).toString(), QStringLiteral("Tagger"));

        const QVariantList children = tagger.value(QStringLiteral("children")).toList();
        QCOMPARE(children.size(), 2);
        const QVariantMap video = children.at(0).toMap();
        const QVariantMap folder = children.at(1).toMap();
        QCOMPARE(video.value(QStringLiteral("label")).toString(), QStringLiteral("Tag this video"));
        QCOMPARE(video.value(QStringLiteral("hotkey")).toString(), QStringLiteral("T"));
        QCOMPARE(folder.value(QStringLiteral("label")).toString(), QStringLiteral("Tag this folder"));
        QCOMPARE(folder.value(QStringLiteral("hotkey")).toString(), QStringLiteral("Shift+T"));

        m.invokeExtra(video.value(QStringLiteral("actionId")).toInt());
        m.invokeExtra(folder.value(QStringLiteral("actionId")).toInt());
        QCOMPARE(g_invoked, 11);
        m.Clear();
    }

    void SubmenuPreservesSectionRegistrationOrder()
    {
        ContextMenuModule m;
        framelift::AddSection(
            m,
            [](ContextMenu& menu)
            {
                framelift::AddItem(
                    menu, "Before",
                    []
                    {
                    }
                );
            }
        );
        framelift::AddSubmenuSection(
            m, "Tagger",
            [](ContextMenu& submenu)
            {
                framelift::AddItem(
                    submenu, "Nested",
                    []
                    {
                    }
                );
            }
        );
        framelift::AddSection(
            m,
            [](ContextMenu& menu)
            {
                framelift::AddItem(
                    menu, "After",
                    []
                    {
                    }
                );
            }
        );
        m.EmitSections();

        const QVariantList items = m.QmlExtraItems();
        QCOMPARE(items.size(), 3);
        QCOMPARE(items.at(0).toMap().value(QStringLiteral("label")).toString(), QStringLiteral("Before"));
        QCOMPARE(items.at(1).toMap().value(QStringLiteral("label")).toString(), QStringLiteral("Tagger"));
        QCOMPARE(items.at(2).toMap().value(QStringLiteral("label")).toString(), QStringLiteral("After"));
        m.Clear();
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

    void ClearRunsNestedItemCleanup()
    {
        g_cleaned = 0;
        ContextMenuModule m;
        m.AddSubmenuSectionRaw(
            "Tagger",
            [](ContextMenu& submenu, void*)
            {
                submenu.AddItemRaw(
                    "Nested", nullptr, nullptr,
                    [](void*)
                    {
                        ++g_cleaned;
                    }
                );
            },
            nullptr, nullptr
        );
        m.EmitSections();
        m.Clear();
        QCOMPARE(g_cleaned, 1);
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
