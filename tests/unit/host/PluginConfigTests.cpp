#include "PluginConfig.h"
#include "TempIni.h"

#include "QtTestRunner.h"
#include <fstream>

#include <QtTest/QtTest>
#include <iterator>
#include <string>

class PluginConfigTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void AbsentIdDefaultsToEnabled()
    {
        const PluginConfig cfg; // nothing loaded
        QVERIFY(cfg.IsEnabled("framelift.anything"));
        QVERIFY(cfg.DisabledIds().empty());
    }

    void MissingFileLeavesEverythingEnabled()
    {
        PluginConfig cfg;
        cfg.Load(UniqueTempPath().string()); // file does not exist
        QVERIFY(cfg.IsEnabled("framelift.overlay"));
        QVERIFY(cfg.DisabledIds().empty());
    }

    void LoadParsesRowsAndDefaultsUnlisted()
    {
        const TempFile f("# header\nframelift.overlay=disabled\nframelift.playlist=enabled\n");

        PluginConfig cfg;
        cfg.Load(f.str());

        QVERIFY(!(cfg.IsEnabled("framelift.overlay")));
        QVERIFY(cfg.IsEnabled("framelift.playlist"));
        QVERIFY(cfg.IsEnabled("framelift.history")); // unlisted ⇒ enabled

        const auto disabled = cfg.DisabledIds();
        QVERIFY((disabled.size()) == (1u));
        QVERIFY(disabled.contains("framelift.overlay"));
    }

    void SetAndSaveRoundTrip()
    {
        const TempFile f;
        {
            PluginConfig cfg;
            cfg.Set("framelift.overlay", false);
            cfg.Set("framelift.playlist", true);
            cfg.Save(f.str());
        }

        PluginConfig reloaded;
        reloaded.Load(f.str());
        QVERIFY(!(reloaded.IsEnabled("framelift.overlay")));
        QVERIFY(reloaded.IsEnabled("framelift.playlist"));
    }

    void EnsureKnownAddsAsEnabledWithoutOverriding()
    {
        PluginConfig cfg;
        cfg.Set("framelift.overlay", false);
        cfg.EnsureKnown({"framelift.overlay", "framelift.history"});

        QVERIFY(!(cfg.IsEnabled("framelift.overlay"))); // existing state preserved
        QVERIFY(cfg.IsEnabled("framelift.history"));    // newly known ⇒ enabled
    }

    void SaveWritesSortedRowsWithHeader()
    {
        const TempFile f;
        PluginConfig cfg;
        cfg.Set("framelift.zzz", true);
        cfg.Set("framelift.aaa", false);
        cfg.Save(f.str());

        std::ifstream in(f.str());
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

        QVERIFY((text.rfind("# FrameLift plugin enablement", 0)) == (0u)); // starts with the header
        const auto aaa = text.find("framelift.aaa=disabled");
        const auto zzz = text.find("framelift.zzz=enabled");
        QVERIFY((aaa) != (std::string::npos));
        QVERIFY((zzz) != (std::string::npos));
        QVERIFY((aaa) < (zzz)); // sorted by id
    }
};

namespace
{
const ::framelift::test::Registrar<PluginConfigTest> kRegisterPluginConfigTest{"PluginConfigTest"};
}

#include "PluginConfigTests.moc"
