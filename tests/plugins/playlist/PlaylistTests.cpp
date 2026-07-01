#include "Playlist.h"

#include "ModuleContext.h"
#include "Settings.h"
#include "TempIni.h"
#include "fakes/FakeMediaPlayer.h"

#include <framelift/ContextHelpers.h>
#include <framelift/Events.h>

#include "QtTestRunner.h"
#include <filesystem>
#include <fstream>

#include <QtTest/QtTest>
#include <iterator>
#include <stdexcept>
#include <string>

namespace
{
// Create a temp directory with the given (empty) files; removed on destruction.
struct TempDir
{
    std::filesystem::path path = std::filesystem::temp_directory_path() /
                                 ("framelift_pl_" + std::to_string(reinterpret_cast<std::uintptr_t>(&path)));

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

class PlaylistTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void AddFileAndCount()
    {
        Playlist pl;
        QVERIFY(pl.Empty());
        pl.AddFile("/a.mp4", "/");
        pl.AddFile("/b.mp4", "/");
        QVERIFY((pl.Count()) == (2));
        QVERIFY(!(pl.Empty()));
    }

    void NextAndPrevWrapAround()
    {
        Playlist pl;
        pl.AddFile("/a.mp4", "/");
        pl.AddFile("/b.mp4", "/");
        pl.AddFile("/c.mp4", "/");
        QVERIFY((pl.Current()) == (-1));

        pl.Next(); // -1 -> 0
        QVERIFY((pl.Current()) == (0));
        pl.Next();
        pl.Next();
        QVERIFY((pl.Current()) == (2));
        pl.Next(); // wraps to 0
        QVERIFY((pl.Current()) == (0));
        pl.Prev(); // wraps to last
        QVERIFY((pl.Current()) == (2));
    }

    void ClearResetsState()
    {
        Playlist pl;
        pl.AddFile("/a.mp4", "/");
        pl.Next();
        pl.Clear();
        QVERIFY(pl.Empty());
        QVERIFY((pl.Current()) == (-1));
    }

    void OpenFileScansDirectoryForVideosOnly()
    {
        const TempDir dir({"a.mp4", "b.mkv", "c.txt", "readme"});

        Playlist pl; // no ctx -> uses default extension lists; watcher arming is skipped
        pl.OpenFile((dir.path / "a.mp4").string().c_str());

        // Non-mixed playlist: only the two video files are picked up (.txt/readme excluded).
        // With no event pump available, OpenFile scans synchronously and applies inline.
        QVERIFY((pl.Count()) == (2));
        QVERIFY((pl.Current()) >= (0)); // the opened file is selected after the scan applies
    }

    // ── LoadFile drives the media player + publishes FileOpenedEvent (with ctx) ────

    void LoadFileDrivesPlayerAndPublishesEvent()
    {
        Settings settings;
        const TempFile ini;
        ModuleContext ctx("pref/", &settings, ini.str());

        FakeMediaPlayer player;
        ctx.RegisterService<IMediaPlayback>(&player);

        std::string opened;
        framelift::Subscribe<FileOpenedEvent>(
            ctx,
            [&](const FileOpenedEvent& e)
            {
                opened = e.path ? e.path : "";
            }
        );

        Playlist pl;
        pl.Install(ctx); // sets ctx_, subscribes to OpenFileRequestEvent
        pl.AddFile("/movies/v.mp4", "/");
        pl.Next(); // activates index 0 → LoadFile

        QVERIFY((player.loadedPath) == ("/movies/v.mp4"));
        QVERIFY((player.loadCount) == (1));
        QVERIFY(!(player.pauseSet));            // SetPause(false) on load
        QVERIFY((opened) == ("/movies/v.mp4")); // FileOpenedEvent published
    }

    // ── First-run persistence: Install writes plugin config to disk ───────────────

    void InstallPersistsSettingsAndKeybindsOnFirstRun()
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
        QVERIFY((text.find("[playlist]")) != (std::string::npos));
        QVERIFY((text.find("autoReload=")) != (std::string::npos));

        // Keybinds live in the plugin's own camelCase section with bare action keys...
        QVERIFY((text.find("[playlist.keybinds]")) != (std::string::npos));
        QVERIFY((text.find("togglePlaylist=L")) != (std::string::npos));
        // ...and must NOT leak into the host-owned [keybinds] section as a prefixed key.
        QVERIFY((text.find("playlist.togglePlaylist")) == (std::string::npos));
    }

    // ── OpenFileRequestEvent drives Playlist (no service interface) ───────────────

    void OpenFileRequestRespectsRebuildFlag()
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
        QVERIFY((player.loadedPath) == (path));
        QVERIFY(pl.Empty());

        // rebuildPlaylist = true → rescan the directory (videos only) and activate.
        ctx.Publish<OpenFileRequestEvent>({path.c_str(), true});
        QVERIFY((pl.Count()) == (2));
        QVERIFY((pl.Current()) >= (0));
    }

    // ── framelift::Guard: a throwing subscriber is contained, not fatal ────────────────

    void ThrowingSubscriberIsContained()
    {
        Settings settings;
        const TempFile ini;
        ModuleContext ctx("pref/", &settings, ini.str());

        framelift::Subscribe<FileOpenedEvent>(
            ctx,
            [](const FileOpenedEvent&)
            {
                throw std::runtime_error("boom");
            }
        );
        int called = 0;
        framelift::Subscribe<FileOpenedEvent>(
            ctx,
            [&](const FileOpenedEvent&)
            {
                called++;
            }
        );

        // The Guard in the Subscribe trampoline logs and swallows the throw;
        // dispatch continues to the next subscriber instead of std::terminate.
        ctx.Publish<FileOpenedEvent>({"/movies/v.mp4"});
        QVERIFY((called) == (1));
    }
};

namespace
{
const ::framelift::test::Registrar<PlaylistTest> kRegisterPlaylistTest{"PlaylistTest"};
}

#include "PlaylistTests.moc"
