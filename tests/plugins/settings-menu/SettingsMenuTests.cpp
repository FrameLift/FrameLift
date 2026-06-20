#include "SettingsMenu.h"

#include "FocusManagerImpl.h"
#include "PluginContext.h"
#include "Settings.h"
#include "TempIni.h"

#include "AudioSettings.h"
#include "CoreSettings.h"
#include "ThemeSettings.h"
#include "UiSettings.h"

#include <cstring>
#include <gtest/gtest.h>
#include <framelift/FocusManager.h>

TEST(SettingsMenuTest, SeedsEditingModelFromContextOnInstall)
{
    Settings settings;
    settings.Get<UiSettings>().panelWidth = 500.f; // non-default host values
    settings.Get<AudioSettings>().dynaudnormFrameLen = 321;
    settings.Get<FilesSettings>().videoExtensions = "avi;mov";

    const TempFile ini;
    PluginContext ctx("pref/", &settings, ini.str());

    SettingsMenu sm;
    sm.Install(ctx); // OnInstall seeds the model via the ABI-stable ctx getters

    EXPECT_FLOAT_EQ(sm.SettingFloat("ui.panelWidth"), 500.f);
    EXPECT_EQ(sm.SettingInt("audio.dynaudnormFrameLen"), 321);
    EXPECT_EQ(sm.SettingString("files.videoExtensions"), "avi;mov");
}

TEST(SettingsMenuTest, SeedsThemeFieldsFromContextOnInstall)
{
    Settings settings;
    settings.Get<ThemeSettings>().preset = "light";
    settings.Get<ThemeSettings>().accentColor = "#112233";
    settings.Get<ThemeSettings>().fontSize = 20.f;

    const TempFile ini;
    PluginContext ctx("pref/", &settings, ini.str());

    SettingsMenu sm;
    sm.Install(ctx);

    EXPECT_EQ(sm.SettingString("theme.preset"), "light");
    EXPECT_EQ(sm.SettingString("theme.accentColor"), "#112233");
    EXPECT_FLOAT_EQ(sm.SettingFloat("theme.fontSize"), 20.f);
}

TEST(SettingsMenuTest, RegistersVisibleThemePage)
{
    Settings settings;
    const TempFile ini;
    PluginContext ctx("pref/", &settings, ini.str());

    SettingsMenu sm;
    sm.Install(ctx);

    struct Finder
    {
        bool found = false;
    };

    Finder f;
    ctx.EnumerateSettingsPages(
        [](const char* title, void (*)(void*, UIContext&), void (*)(void*), void*, bool visible, void* ud)
        {
            if (visible && title && std::strcmp(title, "Theme") == 0)
            {
                static_cast<Finder*>(ud)->found = true;
            }
        },
        &f);

    EXPECT_TRUE(f.found);
}

TEST(SettingsMenuTest, OpenSetsVisible)
{
    SettingsMenu sm;
    EXPECT_FALSE(sm.IsOpen());
    sm.Open();
    EXPECT_TRUE(sm.IsOpen());
}

// Regression for #13: while the dialog is open it must hold keyboard focus and
// swallow key presses so global hotkeys don't fire underneath it.
TEST(SettingsMenuTest, BlocksKeybindsWhileOpen)
{
    Settings settings;
    const TempFile ini;
    PluginContext ctx("pref/", &settings, ini.str());
    FocusManagerImpl fm;
    ctx.RegisterService<FocusManager>(&fm);

    SettingsMenu sm;
    sm.Install(ctx);

    AppEvent key;
    key.type = AppEventType::KeyDown;
    key.AsKey() = {Keys::Space, Mod::None};

    // Closed: no focus held, key passes through to the global hotkey layer.
    EXPECT_EQ(fm.Focused(), nullptr);
    EXPECT_FALSE(sm.HandleKeyDownEvent(key));

    // Open: focus acquired, key swallowed.
    sm.Open();
    EXPECT_EQ(fm.Focused(), static_cast<IModule*>(&sm));
    EXPECT_TRUE(sm.HandleKeyDownEvent(key));

    // Closed again: focus released, key passes through once more.
    sm.Close();
    EXPECT_FALSE(sm.IsOpen());
    EXPECT_EQ(fm.Focused(), nullptr);
    EXPECT_FALSE(sm.HandleKeyDownEvent(key));
}