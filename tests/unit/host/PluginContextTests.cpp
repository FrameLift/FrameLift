#include "PluginConfig.h"
#include "PluginContext.h"
#include "Settings.h"
#include "UiSettings.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <ranges>
#include <string>
#include <vector>

namespace
{
// Construct a PluginContext over a default Settings. The ini path is unused here
// (no Save), so a dummy is fine.
struct Ctx
{
    Settings settings;
    PluginContext ctx{"pref/", &settings, "unused.ini"};
};

std::string GetStr(PluginContext& ctx, const char* key)
{
    const int n = ctx.GetSettingString(key, nullptr, 0); // query length
    std::string s(static_cast<std::size_t>(n), '\0');
    if (n > 0)
    {
        ctx.GetSettingString(key, s.data(), n + 1);
    }
    return s;
}
} // namespace

TEST(PluginContextTest, ReadsDefaultsThroughTypedGetters)
{
    Ctx c;
    EXPECT_FLOAT_EQ(c.ctx.GetSettingFloat("ui.panelWidth"), 320.f);
    EXPECT_TRUE(c.ctx.GetSettingBool("playback.hwdec"));
    EXPECT_EQ(c.ctx.GetSettingInt("audio.dynaudnormFrameLen"), 100);
    EXPECT_EQ(GetStr(c.ctx, "files.videoExtensions").rfind("mp4", 0), 0u);
}

TEST(PluginContextTest, EnumerateSystemFontsIsCachedAndStable)
{
    Ctx c;

    struct Collector
    {
        int count = 0;
        bool allNonEmpty = true;
    };

    auto visit = [](const char* name, const char* path, void* ud)
    {
        auto& col = *static_cast<Collector*>(ud);
        ++col.count;
        if (!name || !name[0] || !path || !path[0])
        {
            col.allNonEmpty = false;
        }
    };

    Collector first;
    c.ctx.EnumerateSystemFonts(visit, &first);

    Collector second;
    c.ctx.EnumerateSystemFonts(visit, &second);

    // Same count on repeat calls (the scan is cached for the session).
    EXPECT_EQ(first.count, second.count);
    // Whatever was found has both a name and a path. (Count may be 0 on a CI box
    // with no fonts — that's fine; the contract is "doesn't crash, stays stable".)
    EXPECT_TRUE(first.allNonEmpty);
}

TEST(PluginContextTest, CommitRoundTripsPerType)
{
    Ctx c;
    c.ctx.CommitSettingFloat("ui.panelWidth", 500.f);
    c.ctx.CommitSettingBool("playback.hwdec", false);
    c.ctx.CommitSettingInt("audio.dynaudnormFrameLen", 250);
    c.ctx.CommitSettingString("files.videoExtensions", "avi;mov");

    EXPECT_FLOAT_EQ(c.ctx.GetSettingFloat("ui.panelWidth"), 500.f);
    EXPECT_FALSE(c.ctx.GetSettingBool("playback.hwdec"));
    EXPECT_EQ(c.ctx.GetSettingInt("audio.dynaudnormFrameLen"), 250);
    EXPECT_EQ(GetStr(c.ctx, "files.videoExtensions"), "avi;mov");
    // The commit also reflects in the underlying Settings object.
    EXPECT_FLOAT_EQ(c.settings.Get<UiSettings>().panelWidth, 500.f);
}

TEST(PluginContextTest, GetSettingStringReportsFullLength)
{
    Ctx c;
    c.ctx.CommitSettingString("files.videoExtensions", "avi;mov");
    EXPECT_EQ(c.ctx.GetSettingString("files.videoExtensions", nullptr, 0), 7); // strlen("avi;mov")
}

TEST(PluginContextTest, GetSettingStringTruncatesToBuffer)
{
    Ctx c;
    c.ctx.CommitSettingString("files.videoExtensions", "avi;mov");
    char buf[4] = {};
    const int len = c.ctx.GetSettingString("files.videoExtensions", buf, 4);
    EXPECT_EQ(len, 7); // returns full length...
    EXPECT_STREQ(buf, "avi"); // ...but writes only cap-1 chars + NUL
}

TEST(PluginContextTest, UnknownKeysYieldZeroOrEmpty)
{
    Ctx c;
    EXPECT_FLOAT_EQ(c.ctx.GetSettingFloat("no.such.key"), 0.f);
    EXPECT_EQ(c.ctx.GetSettingInt("no.such.key"), 0);
    EXPECT_FALSE(c.ctx.GetSettingBool("no.such.key"));
    EXPECT_EQ(c.ctx.GetSettingString("no.such.key", nullptr, 0), 0);
}

TEST(PluginContextTest, WrongTypeKeyDoesNotMatch)
{
    // ui.panelWidth is a float; asking for it as a string should not match.
    Ctx c;
    EXPECT_EQ(GetStr(c.ctx, "ui.panelWidth"), "");
}

// ── Settings file path & reload ───────────────────────────────────────────────

TEST(PluginContextTest, GetSettingsFilePathReportsPathWithBufferContract)
{
    Settings settings;
    PluginContext ctx{"pref/", &settings, "some/dir/settings.ini"};

    // Full length reported regardless of buffer.
    EXPECT_EQ(ctx.GetSettingsFilePath(nullptr, 0), 21); // strlen("some/dir/settings.ini")

    char buf[64] = {};
    EXPECT_EQ(ctx.GetSettingsFilePath(buf, sizeof(buf)), 21);
    EXPECT_STREQ(buf, "some/dir/settings.ini");

    // Truncates to cap-1 chars + NUL but still returns the full length.
    char small[5] = {};
    EXPECT_EQ(ctx.GetSettingsFilePath(small, 5), 21);
    EXPECT_STREQ(small, "some"); // 4 chars + NUL
}

namespace
{
void BumpCounter(void* ud)
{
    ++*static_cast<int*>(ud);
}
} // namespace

TEST(PluginContextTest, ReloadSettingsRereadsFileAndFiresCallbacks)
{
    Settings settings;
    const std::string ini = (std::filesystem::temp_directory_path() / "framelift_test_reload.ini").string();

    // Write a config that overrides one core key; everything else stays default.
    {
        std::ofstream f(ini, std::ios::trunc);
        f << "[ui]\npanelWidth=500\n";
    }

    PluginContext ctx{"pref/", &settings, ini};
    int changeCalls = 0;
    ctx.RegisterSettingsChangeCallback(&BumpCounter, &changeCalls, nullptr);

    // Before reload the in-memory value is still the default.
    EXPECT_FLOAT_EQ(ctx.GetSettingFloat("ui.panelWidth"), 320.f);

    ctx.ReloadSettings();

    EXPECT_FLOAT_EQ(ctx.GetSettingFloat("ui.panelWidth"), 500.f); // picked up from disk
    EXPECT_EQ(changeCalls, 1); // change callbacks fired so the app re-applies

    // A subsequent on-disk edit is reflected on the next reload (reset-then-load
    // means a removed key reverts to its default).
    {
        std::ofstream f(ini, std::ios::trunc);
        f << "[ui]\nslideSpeed=2\n"; // panelWidth removed → back to default
    }
    ctx.ReloadSettings();
    EXPECT_FLOAT_EQ(ctx.GetSettingFloat("ui.panelWidth"), 320.f);
    EXPECT_EQ(changeCalls, 2);

    std::filesystem::remove(ini);
}

// ── Pub/sub reentrancy ────────────────────────────────────────────────────────
// PublishRaw must tolerate callbacks that mutate the subscription list mid-
// dispatch (subscribing reallocates the vector; nested publishes recurse).

namespace
{
struct ReentrancyState
{
    IPluginContext* ctx = nullptr;
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

TEST(PluginContextTest, SubscribeDuringDispatchIsSafeAndDeferred)
{
    Ctx c;
    IPluginContext& ic = c.ctx;
    ReentrancyState st{&ic};

    ic.SubscribeRaw("test.event", &SubscribingHandler, &st);
    ic.SubscribeRaw("test.event", &OtherHandler, &st);

    ic.PublishRaw("test.event", nullptr);
    EXPECT_EQ(st.firstCalls, 1);
    EXPECT_EQ(st.otherCalls, 1); // pre-registered subscriber still dispatched after reallocation
    EXPECT_EQ(st.lateCalls, 0); // added mid-dispatch — must not see the in-flight event

    st.firstCalls = st.otherCalls = 0;
    ic.PublishRaw("test.event", nullptr);
    EXPECT_EQ(st.firstCalls, 1);
    EXPECT_EQ(st.otherCalls, 1);
    EXPECT_EQ(st.lateCalls, 16); // the 16 added on the first publish fire on the next one
}

TEST(PluginContextTest, NestedPublishDispatches)
{
    Ctx c;
    IPluginContext& ic = c.ctx;
    ReentrancyState st{&ic};

    ic.SubscribeRaw("test.event", &NestedPublishHandler, &st);
    ic.SubscribeRaw("test.other", &OtherHandler, &st);

    ic.PublishRaw("test.event", nullptr);
    EXPECT_EQ(st.firstCalls, 1);
    EXPECT_EQ(st.otherCalls, 1); // published from inside the outer dispatch
}

// ── Plugin catalogue ──────────────────────────────────────────────────────────

namespace
{
struct CollectedPlugin
{
    std::string name; // load key
    std::string infoName;
    int version[3];
    std::string publisher;
    bool enabled;
    bool loaded;
    bool loadFailed;
};

void CollectPlugin(const char* name, const FrameLiftPluginInfo& info, bool enabled, bool loaded, bool loadFailed, void* ud)
{
    auto& out = *static_cast<std::vector<CollectedPlugin>*>(ud);
    out.push_back({name, info.name ? info.name : "", {info.version[0], info.version[1], info.version[2]},
                   info.publisher ? info.publisher : "", enabled, loaded, loadFailed});
}

std::vector<CollectedPlugin> Enumerate(PluginContext& ctx)
{
    std::vector<CollectedPlugin> out;
    ctx.EnumeratePlugins(&CollectPlugin, &out);
    return out;
}
} // namespace

TEST(PluginContextTest, EnumeratePluginsReportsLoadedAndDisabledEntries)
{
    Ctx c;
    const FrameLiftPluginInfo alpha{FRAMELIFT_PLUGIN_ABI_MAJOR,
                                    FRAMELIFT_PLUGIN_ABI_MINOR,
                                    FRAMELIFT_PLUGIN_ABI_PATCH,
                                    "framelift.alpha",
                                    "FrameLift.Alpha",
                                    "Alpha",
                                    {2, 3, 4},
                                    "Acme",
                                    "First plugin",
                                    nullptr,
                                    0};
    c.ctx.AddPlugin("framelift.alpha", true, &alpha); // loaded
    c.ctx.AddPlugin("framelift.beta", false, nullptr); // present but disabled
    c.ctx.AddPlugin("Gamma", true, nullptr); // enabled at startup yet not loaded → failed

    const auto got = Enumerate(c.ctx);
    ASSERT_EQ(got.size(), 3u);

    EXPECT_EQ(got[0].name, "framelift.alpha");
    EXPECT_EQ(got[0].infoName, "Alpha");
    EXPECT_EQ(got[0].version[0], 2);
    EXPECT_EQ(got[0].publisher, "Acme");
    EXPECT_TRUE(got[0].enabled);
    EXPECT_TRUE(got[0].loaded);
    EXPECT_FALSE(got[0].loadFailed);

    EXPECT_EQ(got[1].name, "framelift.beta");
    EXPECT_EQ(got[1].infoName, "framelift.beta"); // synthesized name-only descriptor
    EXPECT_EQ(got[1].version[0], 0);
    EXPECT_TRUE(got[1].publisher.empty());
    EXPECT_FALSE(got[1].enabled);
    EXPECT_FALSE(got[1].loaded);
    EXPECT_FALSE(got[1].loadFailed); // disabled, never attempted

    EXPECT_EQ(got[2].name, "Gamma");
    EXPECT_FALSE(got[2].loaded);
    EXPECT_TRUE(got[2].enabled);
    EXPECT_TRUE(got[2].loadFailed); // enabled but missing from the loaded set
}

TEST(PluginContextTest, EnumeratePluginsEmptyByDefault)
{
    Ctx c;
    EXPECT_TRUE(Enumerate(c.ctx).empty());
}

TEST(PluginContextTest, SetPluginEnabledUpdatesCatalogueAndPersists)
{
    Settings settings;
    PluginConfig pluginConfig;
    const std::string pluginsIni =
        (std::filesystem::temp_directory_path() / "framelift_test_pluginsini.ini").string();
    std::filesystem::remove(pluginsIni);
    PluginContext ctx{"pref/", &settings, "unused.ini", &pluginConfig, pluginsIni};

    ctx.AddPlugin("framelift.playlist", true, nullptr);
    ctx.AddPlugin("framelift.history", true, nullptr);

    ctx.SetPluginEnabled("framelift.history", false); // disable one
    ctx.SetPluginEnabled("Unknown", false);           // no-op for unknown names

    // Catalogue reflects the toggle immediately (drives the live checkbox state).
    const auto got = Enumerate(ctx);
    ASSERT_EQ(got.size(), 2u);
    EXPECT_TRUE(got[0].enabled);  // framelift.playlist
    EXPECT_FALSE(got[1].enabled); // framelift.history

    // The opt-out manifest persisted the disable and nothing else.
    PluginConfig reloaded;
    reloaded.Load(pluginsIni);
    EXPECT_FALSE(reloaded.IsEnabled("framelift.history"));
    EXPECT_TRUE(reloaded.IsEnabled("framelift.playlist"));
    EXPECT_TRUE(reloaded.IsEnabled("Unknown")); // never written

    std::filesystem::remove(pluginsIni);
}
