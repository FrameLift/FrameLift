#include "Playlist.h"

#include "ModuleContext.h"
#include "Settings.h"
#include "TempIni.h"
#include "fakes/FakeMediaPlayer.h"

#include <framelift/ContextHelpers.h>
#include <framelift/Events.h>

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <iterator>
#include <stdexcept>
#include <string>

namespace
{
// Create a temp directory with the given (empty) files; removed on destruction.
struct TempDir
{
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("framelift_pl_" + std::to_string(reinterpret_cast<std::uintptr_t>(&path)));

    explicit TempDir(std::initializer_list<const char*> files)
    {
        std::filesystem::create_directories(path);
        for (const char* f : files)
        {
            std::ofstream(path / f) << "x";
        }
    }

    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};
} // namespace

// ── Navigation: needs no context (LoadFile no-ops when ctx_ is null) ──────────

TEST(PlaylistTest, AddFileAndCount)
{
    Playlist pl;
    EXPECT_TRUE(pl.Empty());
    pl.AddFile("/a.mp4");
    pl.AddFile("/b.mp4");
    EXPECT_EQ(pl.Count(), 2);
    EXPECT_FALSE(pl.Empty());
}

TEST(PlaylistTest, NextAndPrevWrapAround)
{
    Playlist pl;
    pl.AddFile("/a.mp4");
    pl.AddFile("/b.mp4");
    pl.AddFile("/c.mp4");
    EXPECT_EQ(pl.Current(), -1);

    pl.Next(); // -1 -> 0
    EXPECT_EQ(pl.Current(), 0);
    pl.Next();
    pl.Next();
    EXPECT_EQ(pl.Current(), 2);
    pl.Next(); // wraps to 0
    EXPECT_EQ(pl.Current(), 0);
    pl.Prev(); // wraps to last
    EXPECT_EQ(pl.Current(), 2);
}

TEST(PlaylistTest, ClearResetsState)
{
    Playlist pl;
    pl.AddFile("/a.mp4");
    pl.Next();
    pl.Clear();
    EXPECT_TRUE(pl.Empty());
    EXPECT_EQ(pl.Current(), -1);
}

TEST(PlaylistTest, OpenFileScansDirectoryForVideosOnly)
{
    const TempDir dir({"a.mp4", "b.mkv", "c.txt", "readme"});

    Playlist pl; // no ctx → uses default extension lists, dir-watch skipped
    pl.OpenFile((dir.path / "a.mp4").string().c_str());

    // Non-mixed playlist: only the two video files are picked up (.txt/readme excluded).
    EXPECT_EQ(pl.Count(), 2);
    EXPECT_GE(pl.Current(), 0); // ActivateByPath found the opened file
}

// ── LoadFile drives the media player + publishes FileOpenedEvent (with ctx) ────

TEST(PlaylistTest, LoadFileDrivesPlayerAndPublishesEvent)
{
    Settings settings;
    const TempFile ini;
    ModuleContext ctx("pref/", &settings, ini.str());

    FakeMediaPlayer player;
    ctx.RegisterService<IMediaPlayback>(&player);

    std::string opened;
    framelift::Subscribe<FileOpenedEvent>(ctx,
                                     [&](const FileOpenedEvent& e)
                                     {
                                         opened = e.path ? e.path : "";
                                     });

    Playlist pl;
    pl.Install(ctx); // sets ctx_, subscribes to OpenFileRequestEvent
    pl.AddFile("/movies/v.mp4");
    pl.Next(); // activates index 0 → LoadFile

    EXPECT_EQ(player.loadedPath, "/movies/v.mp4");
    EXPECT_EQ(player.loadCount, 1);
    EXPECT_FALSE(player.pauseSet); // SetPause(false) on load
    EXPECT_EQ(opened, "/movies/v.mp4"); // FileOpenedEvent published
}

// ── First-run persistence: Install writes plugin config to disk ───────────────

TEST(PlaylistTest, InstallPersistsSettingsAndKeybindsOnFirstRun)
{
    Settings settings;
    const TempFile ini; // unique path, file does not exist yet
    ModuleContext ctx("pref/", &settings, ini.str());

    Playlist pl;
    pl.Install(ctx); // first run → defaults must be written to disk

    std::ifstream in(ini.str());
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    // Plugin settings section materialized with defaults, like the core settings.
    // Section name is camelCase (lowercased first letter) to match core sections.
    EXPECT_NE(text.find("[playlist]"), std::string::npos);
    EXPECT_NE(text.find("autoReload="), std::string::npos);

    // Keybinds live in the plugin's own camelCase section with bare action keys...
    EXPECT_NE(text.find("[playlist.keybinds]"), std::string::npos);
    EXPECT_NE(text.find("togglePlaylist=L"), std::string::npos);
    // ...and must NOT leak into the host-owned [keybinds] section as a prefixed key.
    EXPECT_EQ(text.find("playlist.togglePlaylist"), std::string::npos);
}

// ── OpenFileRequestEvent drives Playlist (no service interface) ───────────────

TEST(PlaylistTest, OpenFileRequestRespectsRebuildFlag)
{
    const TempDir dir({"a.mp4", "b.mkv", "c.txt"});

    Settings settings;
    const TempFile ini;
    ModuleContext ctx("pref/", &settings, ini.str());
    FakeMediaPlayer player;
    ctx.RegisterService<IMediaPlayback>(&player);

    Playlist pl;
    pl.Install(ctx);

    const std::string path = (dir.path / "a.mp4").string();

    // rebuildPlaylist = false → just play the file, no directory scan.
    ctx.Publish<OpenFileRequestEvent>({path.c_str(), false});
    EXPECT_EQ(player.loadedPath, path);
    EXPECT_TRUE(pl.Empty());

    // rebuildPlaylist = true → rescan the directory (videos only) and activate.
    ctx.Publish<OpenFileRequestEvent>({path.c_str(), true});
    EXPECT_EQ(pl.Count(), 2);
    EXPECT_GE(pl.Current(), 0);
}

// ── framelift::Guard: a throwing subscriber is contained, not fatal ────────────────

TEST(PlaylistTest, ThrowingSubscriberIsContained)
{
    Settings settings;
    const TempFile ini;
    ModuleContext ctx("pref/", &settings, ini.str());

    framelift::Subscribe<FileOpenedEvent>(ctx,
                                     [](const FileOpenedEvent&)
                                     {
                                         throw std::runtime_error("boom");
                                     });
    int called = 0;
    framelift::Subscribe<FileOpenedEvent>(ctx,
                                     [&](const FileOpenedEvent&)
                                     {
                                         called++;
                                     });

    // The Guard in the Subscribe trampoline logs and swallows the throw;
    // dispatch continues to the next subscriber instead of std::terminate.
    ctx.Publish<FileOpenedEvent>({"/movies/v.mp4"});
    EXPECT_EQ(called, 1);
}