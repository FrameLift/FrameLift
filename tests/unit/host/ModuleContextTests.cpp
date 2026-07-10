#include "ModuleContext.h"
#include "PluginConfig.h"
#include "Settings.h"
#include "UISettings.h"

#include "QtTestRunner.h"
#include <cstring>
#include <filesystem>
#include <fstream>

#include <QtCore/QObject>
#include <QtTest/QtTest>
#include <ranges>
#include <string>
#include <vector>

namespace
{
// Construct a ModuleContext over a default Settings. The ini path is unused here
// (no Save), so a dummy is fine.
struct Ctx
{
    Settings settings;
    ModuleContext ctx{"pref/", &settings, "unused.ini"};
};

std::string GetStr(ModuleContext& ctx, const char* key)
{
    const int n = ctx.Settings().GetSettingString(key, nullptr, 0); // query length
    std::string s(static_cast<std::size_t>(n), '\0');
    if (n > 0)
    {
        ctx.Settings().GetSettingString(key, s.data(), n + 1);
    }
    return s;
}
} // namespace

namespace
{
void BumpCounter(void* ud)
{
    ++*static_cast<int*>(ud);
}
} // namespace

namespace
{
struct ReentrancyState
{
    IModuleContext* ctx = nullptr;
    int firstCalls = 0;
    int lateCalls = 0;
    int otherCalls = 0;
};

void LateHandler(const void*, void* ud)
{
    static_cast<ReentrancyState*>(ud)->lateCalls++;
}

void SubscribingHandler(const void*, void* ud)
{
    auto* st = static_cast<ReentrancyState*>(ud);
    st->firstCalls++;
    // Add enough subscriptions to force the vector to reallocate mid-dispatch.
    for (int i = 0; i < 16; ++i)
    {
        st->ctx->SubscribeRaw("test.event", &LateHandler, st);
    }
}

void OtherHandler(const void*, void* ud)
{
    static_cast<ReentrancyState*>(ud)->otherCalls++;
}

void NestedPublishHandler(const void*, void* ud)
{
    auto* st = static_cast<ReentrancyState*>(ud);
    st->firstCalls++;
    st->ctx->PublishRaw("test.other", nullptr);
}
} // namespace

namespace
{
struct CollectedSettingsPage
{
    std::string id;
    std::string title;
    std::string qmlUrl;
    QObject* viewModel = nullptr;
    int order = 0;
};

void CollectSettingsPage(const FrameLiftSettingsPageDesc* desc, void* ud)
{
    auto& out = *static_cast<std::vector<CollectedSettingsPage>*>(ud);
    out.push_back(
        {desc->id ? desc->id : "", desc->title ? desc->title : "", desc->qmlUrl ? desc->qmlUrl : "", desc->viewModel,
         desc->order}
    );
}
} // namespace

namespace
{
struct CollectedPlugin
{
    std::string id;
    std::string displayName;
    int version[3];
    std::string publisher;
    std::string description;
    bool enabled;
    bool loaded;
    bool loadFailed;
};

// Copy every string out *during* the callback — the visitor contract is that the
// pointers are valid only for this call.
void CollectPlugin(
    const char* id, const char* displayName, const int* version, const char* publisher, const char* description,
    bool enabled, bool loaded, bool loadFailed, void* ud
)
{
    auto& out = *static_cast<std::vector<CollectedPlugin>*>(ud);
    out.push_back(
        {id,
         displayName ? displayName : "",
         {version[0], version[1], version[2]},
         publisher ? publisher : "",
         description ? description : "",
         enabled,
         loaded,
         loadFailed}
    );
}

std::vector<CollectedPlugin> EnumeratePlugins(ModuleContext& ctx)
{
    std::vector<CollectedPlugin> out;
    ctx.Catalog().EnumeratePlugins(&CollectPlugin, &out);
    return out;
}
} // namespace

class ModuleContextTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void ReadsDefaultsThroughTypedGetters()
    {
        Ctx c;
        QCOMPARE(c.ctx.Settings().GetSettingFloat("ui.panelWidth"), 320.f);
        QVERIFY(c.ctx.Settings().GetSettingBool("playback.subAutoLoad"));
        QVERIFY((GetStr(c.ctx, "audio.normalizeMode")) == ("limiter"));
        QCOMPARE(c.ctx.Settings().GetSettingFloat("audio.limiterLevelIn"), 10.f);
        QVERIFY((c.ctx.Settings().GetSettingInt("audio.dynaudnormFrameLen")) == (100));
        QVERIFY((GetStr(c.ctx, "files.videoExtensions").rfind("mp4", 0)) == (0u));
    }

    void CommitRoundTripsPerType()
    {
        Ctx c;
        c.ctx.Settings().CommitSettingFloat("ui.panelWidth", 500.f);
        c.ctx.Settings().CommitSettingBool("playback.subAutoLoad", false);
        c.ctx.Settings().CommitSettingInt("audio.dynaudnormFrameLen", 250);
        c.ctx.Settings().CommitSettingString("audio.normalizeMode", "dynaudnorm");
        c.ctx.Settings().CommitSettingFloat("audio.limiterLevelIn", 2.5f);
        c.ctx.Settings().CommitSettingString("files.videoExtensions", "avi;mov");

        QCOMPARE(c.ctx.Settings().GetSettingFloat("ui.panelWidth"), 500.f);
        QVERIFY(!(c.ctx.Settings().GetSettingBool("playback.subAutoLoad")));
        QVERIFY((c.ctx.Settings().GetSettingInt("audio.dynaudnormFrameLen")) == (250));
        QVERIFY((GetStr(c.ctx, "audio.normalizeMode")) == ("dynaudnorm"));
        QCOMPARE(c.ctx.Settings().GetSettingFloat("audio.limiterLevelIn"), 2.5f);
        QVERIFY((GetStr(c.ctx, "files.videoExtensions")) == ("avi;mov"));
        // The commit also reflects in the underlying Settings object.
        QCOMPARE(c.settings.Get<UISettings>().panelWidth, 500.f);
    }

    void GetSettingStringReportsFullLength()
    {
        Ctx c;
        c.ctx.Settings().CommitSettingString("files.videoExtensions", "avi;mov");
        QVERIFY((c.ctx.Settings().GetSettingString("files.videoExtensions", nullptr, 0)) == (7)); // strlen("avi;mov")
    }

    void GetSettingStringTruncatesToBuffer()
    {
        Ctx c;
        c.ctx.Settings().CommitSettingString("files.videoExtensions", "avi;mov");
        char buf[4] = {};
        const int len = c.ctx.Settings().GetSettingString("files.videoExtensions", buf, 4);
        QVERIFY((len) == (7));                                // returns full length...
        QVERIFY(::framelift::test::CStringEqual(buf, "avi")); // ...but writes only cap-1 chars + NUL
    }

    void UnknownKeysYieldZeroOrEmpty()
    {
        Ctx c;
        QCOMPARE(c.ctx.Settings().GetSettingFloat("no.such.key"), 0.f);
        QVERIFY((c.ctx.Settings().GetSettingInt("no.such.key")) == (0));
        QVERIFY(!(c.ctx.Settings().GetSettingBool("no.such.key")));
        QVERIFY((c.ctx.Settings().GetSettingString("no.such.key", nullptr, 0)) == (0));
    }

    void WrongTypeKeyDoesNotMatch()
    {
        // ui.panelWidth is a float; asking for it as a string should not match.
        Ctx c;
        QVERIFY((GetStr(c.ctx, "ui.panelWidth")) == (""));
    }

    // ── Settings file path & reload ───────────────────────────────────────────────

    void GetSettingsFilePathReportsPathWithBufferContract()
    {
        Settings settings;
        ModuleContext ctx{"pref/", &settings, "some/dir/settings.ini"};

        // Full length reported regardless of buffer.
        QVERIFY((ctx.Settings().GetSettingsFilePath(nullptr, 0)) == (21)); // strlen("some/dir/settings.ini")

        char buf[64] = {};
        QVERIFY((ctx.Settings().GetSettingsFilePath(buf, sizeof(buf))) == (21));
        QVERIFY(::framelift::test::CStringEqual(buf, "some/dir/settings.ini"));

        // Truncates to cap-1 chars + NUL but still returns the full length.
        char small[5] = {};
        QVERIFY((ctx.Settings().GetSettingsFilePath(small, 5)) == (21));
        QVERIFY(::framelift::test::CStringEqual(small, "some")); // 4 chars + NUL
    }

    void ReloadSettingsRereadsFileAndFiresCallbacks()
    {
        Settings settings;
        const std::string ini = (std::filesystem::temp_directory_path() / "framelift_test_reload.ini").string();

        // Write a config that overrides one core key; everything else stays default.
        {
            std::ofstream f(ini, std::ios::trunc);
            f << "[ui]\npanelWidth=500\n";
        }

        ModuleContext ctx{"pref/", &settings, ini};
        int changeCalls = 0;
        ctx.Settings().RegisterSettingsChangeCallback(&BumpCounter, &changeCalls, nullptr);

        // Before reload the in-memory value is still the default.
        QCOMPARE(ctx.Settings().GetSettingFloat("ui.panelWidth"), 320.f);

        ctx.Settings().ReloadSettings();

        QCOMPARE(ctx.Settings().GetSettingFloat("ui.panelWidth"), 500.f); // picked up from disk
        QVERIFY((changeCalls) == (1)); // change callbacks fired so the app re-applies

        // A subsequent on-disk edit is reflected on the next reload (reset-then-load
        // means a removed key reverts to its default).
        {
            std::ofstream f(ini, std::ios::trunc);
            f << "[ui]\nslideSpeed=2\n"; // panelWidth removed → back to default
        }
        ctx.Settings().ReloadSettings();
        QCOMPARE(ctx.Settings().GetSettingFloat("ui.panelWidth"), 320.f);
        QVERIFY((changeCalls) == (2));

        std::filesystem::remove(ini);
    }

    // ── Pub/sub reentrancy ────────────────────────────────────────────────────────
    // PublishRaw must tolerate callbacks that mutate the subscription list mid-
    // dispatch (subscribing reallocates the vector; nested publishes recurse).

    void SubscribeDuringDispatchIsSafeAndDeferred()
    {
        Ctx c;
        IModuleContext& ic = c.ctx;
        ReentrancyState st{&ic};

        ic.SubscribeRaw("test.event", &SubscribingHandler, &st);
        ic.SubscribeRaw("test.event", &OtherHandler, &st);

        ic.PublishRaw("test.event", nullptr);
        QVERIFY((st.firstCalls) == (1));
        QVERIFY((st.otherCalls) == (1)); // pre-registered subscriber still dispatched after reallocation
        QVERIFY((st.lateCalls) == (0));  // added mid-dispatch — must not see the in-flight event

        st.firstCalls = st.otherCalls = 0;
        ic.PublishRaw("test.event", nullptr);
        QVERIFY((st.firstCalls) == (1));
        QVERIFY((st.otherCalls) == (1));
        QVERIFY((st.lateCalls) == (16)); // the 16 added on the first publish fire on the next one
    }

    void NestedPublishDispatches()
    {
        Ctx c;
        IModuleContext& ic = c.ctx;
        ReentrancyState st{&ic};

        ic.SubscribeRaw("test.event", &NestedPublishHandler, &st);
        ic.SubscribeRaw("test.other", &OtherHandler, &st);

        ic.PublishRaw("test.event", nullptr);
        QVERIFY((st.firstCalls) == (1));
        QVERIFY((st.otherCalls) == (1)); // published from inside the outer dispatch
    }

    void SettingsPagesRegisterEnumerateAndClear()
    {
        Ctx c;
        QObject first;
        QObject second;

        c.ctx.Settings().RegisterSettingsPage("playlist", "Playlist", "qrc:/PlaylistSettings.qml", &first, 20);
        c.ctx.Settings().RegisterSettingsPage("audio", "Audio", "qrc:/AudioSettings.qml", &second, 10);

        std::vector<CollectedSettingsPage> pages;
        c.ctx.Settings().EnumerateSettingsPages(&CollectSettingsPage, &pages);

        QVERIFY((pages.size()) == (2u));
        QVERIFY((pages[0].id) == ("playlist"));
        QVERIFY((pages[0].title) == ("Playlist"));
        QVERIFY((pages[0].qmlUrl) == ("qrc:/PlaylistSettings.qml"));
        QVERIFY((pages[0].viewModel) == (&first));
        QVERIFY((pages[0].order) == (20));
        QVERIFY((pages[1].id) == ("audio"));

        c.ctx.ClearSubscriptions();
        pages.clear();
        c.ctx.Settings().EnumerateSettingsPages(&CollectSettingsPage, &pages);
        QVERIFY(pages.empty());
    }

    void KeybindEntryRegistrationToleratesNullArgs()
    {
        Ctx c;
        auto& s = c.ctx.Settings();

        // ABI-facing: a plugin may pass null for any pointer arg. None must be used
        // to construct a std::string (label/actionName were previously unguarded).
        s.RegisterKeybindEntry(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
        // A null visitor must be tolerated, like every sibling enumerator.
        s.EnumerateKeybindEntries(nullptr, nullptr);

        struct Seen
        {
            int count = 0;
            bool anyNull = false;
        };

        Seen seen;
        s.EnumerateKeybindEntries(
            [](const char* label, const char* action, const char* (*)(void*), void (*)(void*, const char*), void*,
               const char* group, const char* def, void* ud)
            {
                auto* r = static_cast<Seen*>(ud);
                r->count++;
                r->anyNull = !label || !action || !group || !def;
            },
            &seen
        );
        QVERIFY((seen.count) == (1));
        QVERIFY(!(seen.anyNull)); // stored as empty strings, never null
    }

    // ── Plugin catalogue ──────────────────────────────────────────────────────────

    void EnumeratePluginsReportsLoadedAndDisabledEntries()
    {
        Ctx c;

        PluginCatalog::PluginCatalogEntry alpha;
        alpha.id = "framelift.alpha";
        alpha.displayName = "Alpha";
        alpha.version[0] = 2;
        alpha.version[1] = 3;
        alpha.version[2] = 4;
        alpha.publisher = "Acme";
        alpha.description = "First plugin";
        alpha.loaded = true;
        c.ctx.Catalog().AddPlugin(std::move(alpha));

        PluginCatalog::PluginCatalogEntry beta; // present but disabled
        beta.id = "framelift.beta";
        beta.enabled = false;
        beta.loaded = false;
        c.ctx.Catalog().AddPlugin(std::move(beta));

        PluginCatalog::PluginCatalogEntry gamma; // enabled yet not loaded -> failed
        gamma.id = "framelift.gamma";
        gamma.loaded = false;
        c.ctx.Catalog().AddPlugin(std::move(gamma));

        const auto plugins = EnumeratePlugins(c.ctx);
        QVERIFY((plugins.size()) == (3u));
        QVERIFY((plugins[0].id) == ("framelift.alpha"));
        QVERIFY((plugins[0].displayName) == ("Alpha"));
        QVERIFY((plugins[0].version[0]) == (2));
        QVERIFY((plugins[0].publisher) == ("Acme"));
        QVERIFY(plugins[0].enabled);
        QVERIFY(plugins[0].loaded);
        QVERIFY(!(plugins[0].loadFailed));

        QVERIFY((plugins[1].id) == ("framelift.beta"));
        QVERIFY(!(plugins[1].enabled));
        QVERIFY(!(plugins[1].loaded));
        QVERIFY(!(plugins[1].loadFailed));

        QVERIFY((plugins[2].id) == ("framelift.gamma"));
        QVERIFY(plugins[2].enabled);
        QVERIFY(!(plugins[2].loaded));
        QVERIFY(plugins[2].loadFailed);
    }

    void EnumeratePluginsEmptyByDefault()
    {
        Ctx c;
        QVERIFY(EnumeratePlugins(c.ctx).empty());
    }

    void SetPluginEnabledUpdatesCatalogueAndPersists()
    {
        Settings settings;
        PluginConfig pluginConfig;
        const std::string pluginsIni =
            (std::filesystem::temp_directory_path() / "framelift_test_pluginsini.ini").string();
        std::filesystem::remove(pluginsIni);
        ModuleContext ctx{"pref/", &settings, "unused.ini", &pluginConfig, pluginsIni};

        PluginCatalog::PluginCatalogEntry plugin;
        plugin.id = "framelift.media";
        plugin.loaded = true;
        ctx.Catalog().AddPlugin(std::move(plugin));

        ctx.Catalog().SetPluginEnabled("framelift.media", false);
        ctx.Catalog().SetPluginEnabled("unknown.plugin", false);

        // Catalogue reflects the toggle immediately (drives the live checkbox state).
        const auto plugins = EnumeratePlugins(ctx);
        QVERIFY((plugins.size()) == (1u));
        QVERIFY(!(plugins[0].enabled));

        // The opt-out manifest persisted the disable and nothing else.
        PluginConfig reloaded;
        reloaded.Load(pluginsIni);
        QVERIFY(!(reloaded.IsEnabled("framelift.media")));
        QVERIFY(reloaded.IsEnabled("unknown.plugin"));

        std::filesystem::remove(pluginsIni);
    }
};

namespace
{
const ::framelift::test::Registrar<ModuleContextTest> kRegisterModuleContextTest{"ModuleContextTest"};
}

#include "ModuleContextTests.moc"
