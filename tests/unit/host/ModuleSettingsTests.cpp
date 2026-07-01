#include "ModuleSettingsImpl.h"
#include "TempIni.h"

#include "QtTestRunner.h"

#include <QtTest/QtTest>
#include <fstream>
#include <iterator>
#include <string>

class ModuleSettingsTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void MissingSectionReturnsDefaults()
    {
        const TempFile f; // empty, non-existent section
        const ModuleSettingsImpl ps("MyPlugin", f.str());

        QVERIFY(!(ps.WasLoaded()));
        QVERIFY((ps.KeyCount()) == (0));
        QVERIFY(::framelift::test::CStringEqual(ps.GetString("name", "fallback"), "fallback"));
        QVERIFY((ps.GetInt("count", 7)) == (7));
        QCOMPARE(ps.GetFloat("ratio", 2.5f), 2.5f);
        QVERIFY(ps.GetBool("flag", true));
    }

    void SaveThenReloadRoundTrips()
    {
        const TempFile f;
        {
            ModuleSettingsImpl ps("MyPlugin", f.str());
            ps.SetString("name", "FrameLift");
            ps.SetInt("count", 42);
            ps.SetBool("flag", true);
            ps.SetFloat("ratio", 1.5f);
            QVERIFY((ps.KeyCount()) == (4));
            ps.Save();
        }

        const ModuleSettingsImpl reloaded("MyPlugin", f.str());
        QVERIFY(reloaded.WasLoaded());
        QVERIFY((reloaded.KeyCount()) == (4));
        QVERIFY(::framelift::test::CStringEqual(reloaded.GetString("name"), "FrameLift"));
        QVERIFY((reloaded.GetInt("count")) == (42));
        QVERIFY(reloaded.GetBool("flag"));
        QCOMPARE(reloaded.GetFloat("ratio"), 1.5f);
    }

    void SectionsAreIsolated()
    {
        const TempFile f;
        {
            ModuleSettingsImpl a("SectionA", f.str());
            a.SetString("k", "valueA");
            a.Save();
        }
        {
            ModuleSettingsImpl b("SectionB", f.str());
            b.SetString("k", "valueB");
            b.Save();
        }

        const ModuleSettingsImpl a("SectionA", f.str());
        const ModuleSettingsImpl b("SectionB", f.str());
        QVERIFY(::framelift::test::CStringEqual(a.GetString("k"), "valueA"));
        QVERIFY(::framelift::test::CStringEqual(b.GetString("k"), "valueB"));

        // A section that was never written is not "loaded".
        const ModuleSettingsImpl c("SectionC", f.str());
        QVERIFY(!(c.WasLoaded()));
    }

    void BadNumericFallsBackToDefault()
    {
        const TempFile f("[MyPlugin]\ncount=notanumber\n");
        const ModuleSettingsImpl ps("MyPlugin", f.str());

        QVERIFY(ps.WasLoaded());
        QVERIFY((ps.GetInt("count", -1)) == (-1)); // stoi throws → default
        QCOMPARE(ps.GetFloat("count", -1.f), -1.f);
    }

    // ── Incorrect setting type handling (issue #2) ──────────────────────────────────
    // Values are stored as strings; the typed getters reinterpret them on read. A
    // value read as the "wrong" type must never crash — it either converts where it
    // can or falls back to the caller's default. These tests pin that behavior.

    void GetIntOutOfRangeFallsBack()
    {
        const TempFile f("[MyPlugin]\ncount=999999999999\n");
        const ModuleSettingsImpl ps("MyPlugin", f.str());

        // std::stoi throws std::out_of_range → caught → returns caller's default.
        QVERIFY((ps.GetInt("count", -1)) == (-1));
    }

    void GetIntFromFloatStringTruncates()
    {
        const TempFile f("[MyPlugin]\ncount=3.14\n");
        const ModuleSettingsImpl ps("MyPlugin", f.str());

        // std::stoi parses the leading integer prefix and stops — no throw.
        QVERIFY((ps.GetInt("count")) == (3));
    }

    void GetBoolOnlyOneIsTrue()
    {
        for (const char* token : {"true", "0", "2", "yes"})
        {
            const TempFile f(std::string("[MyPlugin]\nflag=") + token + "\n");
            const ModuleSettingsImpl ps("MyPlugin", f.str());
            QVERIFY2(!(ps.GetBool("flag", true)), (std::string("token=") + token).c_str());
        }

        const TempFile one("[MyPlugin]\nflag=1\n");
        const ModuleSettingsImpl ps("MyPlugin", one.str());
        QVERIFY(ps.GetBool("flag", false));
    }

    void CrossTypeReadsAreSafe()
    {
        const TempFile f;
        ModuleSettingsImpl ps("MyPlugin", f.str());

        // An int stored, then read as a float.
        ps.SetInt("n", 42);
        QCOMPARE(ps.GetFloat("n"), 42.f);

        // A bool serializes as "1"/"0", so reading it back as an int yields 1/0.
        ps.SetBool("b", true);
        QVERIFY((ps.GetInt("b")) == (1));
        ps.SetBool("b", false);
        QVERIFY((ps.GetInt("b")) == (0));
    }

    void EmptyValueFallsBackToDefault()
    {
        const TempFile f("[MyPlugin]\nx=\n");
        const ModuleSettingsImpl ps("MyPlugin", f.str());

        QVERIFY(ps.WasLoaded());
        QVERIFY((ps.GetInt("x", -1)) == (-1));  // std::stoi("") throws → default
        QCOMPARE(ps.GetFloat("x", -1.f), -1.f); // std::stof("") throws → default
    }

    // Plugin keybinds live in their own [<Plugin>.keybinds] section, so writing them
    // must leave the host-owned [keybinds] section values intact.
    void ModuleKeybindSectionLeavesHostKeybindsIntact()
    {
        const TempFile f(
            "[keybinds]\n# Key combo to play/pause.\ntogglePause=Space\n# Key combo to quit.\nquit=Ctrl+Q\n"
        );

        {
            ModuleSettingsImpl ps("history.keybinds", f.str());
            ps.SetString("toggleHistory", "H");
            ps.Save();
        }

        std::ifstream in(f.str());
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

        // The module's own camelCase section was created with the bare action key.
        QVERIFY((text.find("[history.keybinds]")) != (std::string::npos));
        QVERIFY((text.find("toggleHistory=H")) != (std::string::npos));

        // The host [keybinds] section values survive.
        QVERIFY((text.find("[keybinds]")) != (std::string::npos));
        QVERIFY((text.find("togglePause=Space")) != (std::string::npos));
        QVERIFY((text.find("quit=Ctrl+Q")) != (std::string::npos));

        // Reloading the plugin section yields the written value.
        const ModuleSettingsImpl reloaded("history.keybinds", f.str());
        QVERIFY(reloaded.WasLoaded());
        QVERIFY(::framelift::test::CStringEqual(reloaded.GetString("toggleHistory"), "H"));
    }
};

namespace
{
const ::framelift::test::Registrar<ModuleSettingsTest> kRegisterModuleSettingsTest{"ModuleSettingsTest"};
}

#include "ModuleSettingsTests.moc"
