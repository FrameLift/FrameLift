#include "SettingsMenu.h"

#include "KeybindList.h"

#include "ModuleContext.h"
#include "Settings.h"
#include "TempIni.h"

#include "AudioSettings.h"
#include "CoreSettings.h"
#include "ThemeSettings.h"
#include "UISettings.h"

#include "QtTestRunner.h"
#include <QtCore/QVariantList>
#include <QtCore/QVariantMap>
#include <QtCore/Qt>
#include <cstring>

#include <QtTest/QtTest>
#include <ranges>

namespace
{
bool PagesContainId(const QVariantList& pages, const QString& id)
{
    return std::ranges::any_of(
        pages,
        [&](const QVariant& page)
        {
            return page.toMap().value(QStringLiteral("id")).toString() == id;
        }
    );
}
} // namespace

namespace
{
QVariantMap FindKeybindEntry(const QVariantList& entries, const QString& action)
{
    for (const QVariant& v : entries)
    {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("action")).toString() == action)
        {
            return m;
        }
    }
    return {};
}
} // namespace

class SettingsMenuTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void SeedsEditingModelFromContextOnInstall()
    {
        Settings settings;
        settings.Get<UISettings>().panelWidth = 500.f; // non-default host values
        settings.Get<AudioSettings>().normalizeMode = "dynaudnorm";
        settings.Get<AudioSettings>().limiterLevelIn = 2.5f;
        settings.Get<AudioSettings>().dynaudnormFrameLen = 321;
        settings.Get<FilesSettings>().videoExtensions = "avi;mov";

        const TempFile ini;
        ModuleContext ctx("pref/", &settings, ini.str());

        SettingsMenu sm;
        sm.Install(ctx); // OnInstall seeds the model via the ABI-stable ctx getters

        QCOMPARE(sm.SettingFloat("ui.panelWidth"), 500.f);
        QVERIFY((sm.SettingString("audio.normalizeMode")) == ("dynaudnorm"));
        QCOMPARE(sm.SettingFloat("audio.limiterLevelIn"), 2.5f);
        QVERIFY((sm.SettingInt("audio.dynaudnormFrameLen")) == (321));
        QVERIFY((sm.SettingString("files.videoExtensions")) == ("avi;mov"));
    }

    void SeedsThemeFieldsFromContextOnInstall()
    {
        Settings settings;
        settings.Get<ThemeSettings>().preset = "light";
        settings.Get<ThemeSettings>().accentColor = "#112233";

        const TempFile ini;
        ModuleContext ctx("pref/", &settings, ini.str());

        SettingsMenu sm;
        sm.Install(ctx);

        QVERIFY((sm.SettingString("theme.preset")) == ("light"));
        QVERIFY((sm.SettingString("theme.accentColor")) == ("#112233"));
    }

    void ExposesThemePageToQml()
    {
        Settings settings;
        const TempFile ini;
        ModuleContext ctx("pref/", &settings, ini.str());

        SettingsMenu sm;
        sm.Install(ctx);

        QVERIFY(PagesContainId(sm.QmlPages(), QStringLiteral("theme")));
    }

    void DoesNotInventModulePagesFromRawFields()
    {
        Settings settings;
        const TempFile ini;
        ModuleContext ctx("pref/", &settings, ini.str());

        FrameLiftModuleSettingDesc desc{
            "fakePlugin.enabled",
            0,
            "",
            "1",
            [](void*) -> const char*
            {
                return "1";
            },
            [](void*, const char*)
            {
            },
            nullptr
        };
        ctx.Settings().RegisterModuleSetting(&desc);

        SettingsMenu sm;
        sm.Install(ctx);

        QVERIFY(!(PagesContainId(sm.QmlPages(), QStringLiteral("fakePlugin"))));
    }

    void OpenSetsVisible()
    {
        SettingsMenu sm;
        QVERIFY(!(sm.IsOpen()));
        sm.Open();
        QVERIFY(sm.IsOpen());
    }

    // Regression for #13: while the dialog is open it swallows key presses so global
    // hotkeys don't fire underneath it.
    void SwallowsKeybindsWhileOpen()
    {
        Settings settings;
        const TempFile ini;
        ModuleContext ctx("pref/", &settings, ini.str());

        SettingsMenu sm;
        sm.Install(ctx);

        AppEvent key;
        key.type = AppEventType::KeyDown;
        key.AsKey() = {Keys::Space, Mod::None};

        // Closed: key passes through to the global hotkey layer.
        QVERIFY(!(sm.HandleKeyDownEvent(key)));

        // Open: key swallowed.
        sm.Open();
        QVERIFY(sm.HandleKeyDownEvent(key));

        // Closed again: key passes through once more.
        sm.Close();
        QVERIFY(!(sm.IsOpen()));
        QVERIFY(!(sm.HandleKeyDownEvent(key)));
    }

    // ── Keybind list helpers (multi-key-per-action editing) ───────────────────────
};

namespace
{
const ::framelift::test::Registrar<SettingsMenuTest> kRegisterSettingsMenuTest{"SettingsMenuTest"};
}

class KeybindListTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void SplitTrimsAndDropsEmpty()
    {
        const auto keys = keybinds::Split(" Ctrl+F ; F2 ;");
        QVERIFY((keys.size()) == (2u));
        QVERIFY((keys[0]) == ("Ctrl+F"));
        QVERIFY((keys[1]) == ("F2"));
        QVERIFY(keybinds::Split("").empty());
    }

    void ContainsMatchesWholeTokens()
    {
        QVERIFY(keybinds::Contains("Ctrl+F;F2", "F2"));
        QVERIFY(!(keybinds::Contains("Ctrl+F;F2", "F")));
    }

    void SlotReadsByPosition()
    {
        QVERIFY((keybinds::Slot("Ctrl+F;F2", 0)) == ("Ctrl+F"));
        QVERIFY((keybinds::Slot("Ctrl+F;F2", 1)) == ("F2"));
        QVERIFY((keybinds::Slot("Ctrl+F", 1)) == ("")); // unset alternate
    }

    void SetSlotEditsOneSlot()
    {
        QVERIFY((keybinds::SetSlot("", 0, "F")) == ("F"));        // set primary
        QVERIFY((keybinds::SetSlot("F", 1, "F2")) == ("F;F2"));   // add alternate
        QVERIFY((keybinds::SetSlot("F;F2", 0, "")) == ("F2"));    // clear primary compacts
        QVERIFY((keybinds::SetSlot("F;F2", 1, "")) == ("F"));     // clear alternate
        QVERIFY((keybinds::SetSlot("F;F2", 0, "G")) == ("G;F2")); // replace primary
    }

    void SetSlotAvoidsDuplicateAcrossSlots()
    {
        // Assigning a key already held by the other slot moves it rather than duplicating.
        QVERIFY((keybinds::SetSlot("F;F2", 0, "F2")) == ("F2"));
    }

    // ── Keybinds page capture (Qt-driven, into drafts) ────────────────────────────
};

namespace
{
const ::framelift::test::Registrar<KeybindListTest> kRegisterKeybindListTest{"KeybindListTest"};
}

class SettingsMenuKeybindsTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void CapturesCoreKeyIntoDraftSlots()
    {
        Settings settings;
        const TempFile ini;
        ModuleContext ctx("pref/", &settings, ini.str());
        SettingsMenu sm;
        sm.Install(ctx);

        // Primary slot: Ctrl+P (an unbound chord) replaces the default.
        sm.BeginCapture(QStringLiteral("togglePause"), 0);
        QVERIFY(sm.Capturing());
        sm.CaptureKey(static_cast<int>(Keys::P), static_cast<int>(Qt::ControlModifier));
        QVERIFY(!(sm.Capturing()));
        QVERIFY((sm.SettingString("keybinds.togglePause")) == ("Ctrl+P"));

        // Alternate slot: add G.
        sm.BeginCapture(QStringLiteral("togglePause"), 1);
        sm.CaptureKey(static_cast<int>(Keys::G), 0);
        QVERIFY((sm.SettingString("keybinds.togglePause")) == ("Ctrl+P;G"));

        // Clearing the alternate compacts back to just the primary.
        sm.ClearKeybindSlot(QStringLiteral("togglePause"), 1);
        QVERIFY((sm.SettingString("keybinds.togglePause")) == ("Ctrl+P"));
    }

    void ReseedDuringCaptureResolvesFreshSlot()
    {
        Settings settings;
        const TempFile ini;
        ModuleContext ctx("pref/", &settings, ini.str());
        SettingsMenu sm;
        sm.Install(ctx);

        // Begin capturing, then force the keybind model to be reseeded mid-capture:
        // any read of the keybind list re-runs SeedFromContext while the page is not
        // dirty, reallocating the container the capture target lives in. The key
        // press must re-resolve the target slot by name rather than writing through
        // a pointer cached at BeginCapture time (which would be a use-after-free).
        sm.BeginCapture(QStringLiteral("togglePause"), 0);
        QVERIFY(sm.Capturing());
        (void)sm.CoreKeybindEntries(); // triggers RefreshFields -> SeedFromContext

        sm.CaptureKey(static_cast<int>(Keys::P), static_cast<int>(Qt::ControlModifier));
        QVERIFY(!(sm.Capturing()));
        QVERIFY((sm.SettingString("keybinds.togglePause")) == ("Ctrl+P"));
    }

    void RejectsKeyAlreadyBoundElsewhere()
    {
        Settings settings;
        const TempFile ini;
        ModuleContext ctx("pref/", &settings, ini.str());
        SettingsMenu sm;
        sm.Install(ctx);

        // "M" is the default for toggleMute — capturing it for togglePause must conflict.
        sm.BeginCapture(QStringLiteral("togglePause"), 0);
        sm.CaptureKey(static_cast<int>(Keys::M), 0);

        QVERIFY(!(sm.Capturing()));
        QVERIFY(!(sm.KeybindConflict().isEmpty()));
        QVERIFY((sm.SettingString("keybinds.togglePause")) == ("Space")); // unchanged default
    }

    void ExposesCoreAndPluginGroups()
    {
        Settings settings;
        const TempFile ini;
        ModuleContext ctx("pref/", &settings, ini.str());
        SettingsMenu sm;
        sm.Install(ctx);

        const QVariantList core = sm.CoreKeybindEntries();
        QVERIFY(!(FindKeybindEntry(core, QStringLiteral("togglePause")).isEmpty()));

        // SettingsMenu's own "openSettings" keybind is auto-registered via ModuleBase and
        // must surface under its module's group.
        const QVariantList groups = sm.PluginKeybindGroups();
        QVERIFY(!(groups.isEmpty()));
        bool foundOpenSettings = false;
        for (const QVariant& g : groups)
        {
            const QVariantList entries = g.toMap().value(QStringLiteral("entries")).toList();
            if (!FindKeybindEntry(entries, QStringLiteral("openSettings")).isEmpty())
            {
                foundOpenSettings = true;
                QVERIFY((g.toMap().value(QStringLiteral("title")).toString()) == (QStringLiteral("SettingsMenu")));
            }
        }
        QVERIFY(foundOpenSettings);
    }

    void ResetRestoresPluginDefault()
    {
        Settings settings;
        const TempFile ini;
        ModuleContext ctx("pref/", &settings, ini.str());
        SettingsMenu sm;
        sm.Install(ctx);

        // Rebind openSettings, then Reset should restore its factory default ("Ctrl+Comma").
        sm.BeginCapture(QStringLiteral("openSettings"), 0);
        sm.CaptureKey(static_cast<int>(Keys::F8), 0);

        auto openSettingsPrimary = [&]
        {
            const QVariantList groups = sm.PluginKeybindGroups();
            for (const QVariant& g : groups)
            {
                const QVariantMap e = FindKeybindEntry(
                    g.toMap().value(QStringLiteral("entries")).toList(), QStringLiteral("openSettings")
                );
                if (!e.isEmpty())
                {
                    return e.value(QStringLiteral("primary")).toString();
                }
            }
            return QString{};
        };
        QVERIFY((openSettingsPrimary()) == (QStringLiteral("F8")));

        sm.resetActivePage();
        QVERIFY((openSettingsPrimary()) == (QStringLiteral("Ctrl+Comma")));
    }
};

namespace
{
const ::framelift::test::Registrar<SettingsMenuKeybindsTest> kRegisterSettingsMenuKeybindsTest{
    "SettingsMenuKeybindsTest"
};
}

#include "SettingsMenuTests.moc"
