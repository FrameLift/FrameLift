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

#include <QtQml/QQmlComponent>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>
#include <QtTest/QtTest>
#include <cstring>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

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

class FakeEventPump final : public IEventPump
{
public:
    [[nodiscard]] uint32_t RegisterCustomEventType() noexcept override
    {
        return nextEventType_++;
    }

    void PushCustomEvent(uint32_t, void*) noexcept override
    {
    }

    void PushQuitEvent() noexcept override
    {
    }

private:
    uint32_t nextEventType_ = 1;
};

// Minimal IMediaTags: returns a fixed tag list for one path, nothing for others.
class FakeMediaTags final : public IMediaTags
{
public:
    std::string taggedPath;
    std::vector<std::string> tags;

    [[nodiscard]] int GetTagCount(const char* path) const noexcept override
    {
        return (path && taggedPath == path) ? static_cast<int>(tags.size()) : 0;
    }

    [[nodiscard]] int GetTag(
        const char* path, int index, char* buf, int cap, float* confidence, char* modelBuf, int modelCap
    ) const noexcept override
    {
        if (!path || taggedPath != path || index < 0 || index >= static_cast<int>(tags.size()))
        {
            return -1;
        }
        if (confidence)
        {
            *confidence = 0.9f;
        }
        if (modelBuf && modelCap > 0)
        {
            modelBuf[0] = '\0';
        }
        const std::string& s = tags[static_cast<std::size_t>(index)];
        const int len = static_cast<int>(s.size());
        if (buf && cap > 0)
        {
            const int n = len < cap - 1 ? len : cap - 1;
            std::memcpy(buf, s.data(), static_cast<std::size_t>(n));
            buf[n] = '\0';
        }
        return len;
    }

    [[nodiscard]] bool HasTag(const char* path, const char* tag, float) const noexcept override
    {
        if (!path || taggedPath != path || !tag)
        {
            return false;
        }
        for (const std::string& t : tags)
        {
            if (t == tag)
            {
                return true;
            }
        }
        return false;
    }
};

QStringList RowTags(const QVariantList& rows, const QString& path)
{
    for (const QVariant& v : rows)
    {
        const QVariantMap row = v.toMap();
        if (row.value("path").toString() == path)
        {
            return row.value("tags").toStringList();
        }
    }
    return {};
}

QString RowPath(const QVariantList& rows, const int index)
{
    if (index < 0 || index >= rows.size())
    {
        return {};
    }
    return rows[index].toMap().value("path").toString();
}
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

    void QmlBindingRemainsSafeWhenOpenFileClearsTheProjection()
    {
        Playlist pl;
        for (int i = 0; i < 114; ++i)
        {
            pl.AddFile("/old/" + std::to_string(i) + ".mp4", "/old");
        }

        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("playlistModel"), &pl);
        QQmlComponent component(&engine);
        component.setData(
            R"(
                import QtQml
                QtObject {
                    property int observedCount: playlistModel.entries.length
                }
            )",
            QUrl()
        );
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> binding(component.create());
        QVERIFY(binding != nullptr);
        QCOMPARE(binding->property("observedCount").toInt(), 114);

        const TempDir dir({"new-a.mp4", "new-b.mkv"});
        pl.OpenFile((dir.path / "new-a.mp4").string().c_str());

        QCOMPARE(pl.Count(), 2);
        QCOMPARE(binding->property("observedCount").toInt(), 2);
    }

    // Rows carry AI tags from IMediaTags when the service is present; rows for untagged
    // files carry an empty list.
    void QmlEntriesCarryMediaTags()
    {
        FakeMediaTags tags;
        tags.taggedPath = "/a.mp4";
        tags.tags = {"beach", "ocean"};

        Settings settings;
        const TempFile ini;
        ModuleContext ctx("pref/", &settings, ini.str());
        ctx.RegisterService<IMediaTags>(&tags);

        Playlist pl;
        pl.Install(ctx); // discovers IMediaTags
        pl.AddFile("/a.mp4", "/");
        pl.AddFile("/b.mp4", "/");

        const QVariantList rows = pl.QmlEntries();
        QCOMPARE(RowTags(rows, "/a.mp4"), (QStringList{"beach", "ocean"}));
        QVERIFY(RowTags(rows, "/b.mp4").isEmpty());
    }

    // Without the service, rows still build and simply carry no tags.
    void QmlEntriesWithoutTagsServiceAreEmpty()
    {
        Playlist pl;
        pl.AddFile("/a.mp4", "/");
        const QVariantList rows = pl.QmlEntries();
        QVERIFY(RowTags(rows, "/a.mp4").isEmpty());
    }

    void SearchFiltersFilenamePathAndSubfolderCaseInsensitively()
    {
        Playlist pl;
        pl.AddFile("/Movies/Alpha.mp4", "/Movies");
        pl.AddFile("/Shows/Beta.mkv", "/Shows");
        QCOMPARE(pl.Count(), 2);

        pl.SetSearch(QStringLiteral("movies"));
        QCOMPARE(pl.QmlEntries().size(), 1);
        QCOMPARE(RowPath(pl.QmlEntries(), 0), QStringLiteral("/Movies/Alpha.mp4"));

        pl.SetSearch(QStringLiteral("BETA"));
        QCOMPARE(pl.QmlEntries().size(), 1);
        QCOMPARE(RowPath(pl.QmlEntries(), 0), QStringLiteral("/Shows/Beta.mkv"));

        pl.SetSearch(QStringLiteral("missing"));
        QVERIFY(pl.QmlEntries().isEmpty());
        QCOMPARE(pl.Count(), 2);

        pl.SetSearch(QString());
        QCOMPARE(pl.QmlEntries().size(), 2);
        QVERIFY(pl.Search().isEmpty());
    }

    void FilteredActivationMapsBackToTheSourcePlaylist()
    {
        Playlist pl;
        pl.AddFile("/a.mp4", "/");
        pl.AddFile("/b.mp4", "/");
        pl.AddFile("/c.mp4", "/");
        pl.Next();
        QCOMPARE(pl.Current(), 0);

        pl.SetSearch(QStringLiteral("c.mp4"));
        QCOMPARE(pl.QmlEntries().size(), 1);
        QCOMPARE(pl.QmlCurrentIndex(), -1);

        pl.activateIndex(0);
        QCOMPARE(pl.Current(), 2);
        QCOMPARE(pl.QmlCurrentIndex(), 0);
        QVERIFY(pl.QmlEntries().front().toMap().value("current").toBool());
    }

    void SearchProjectionSurvivesSortAndShuffle()
    {
        Playlist pl;
        pl.AddFile("/z/match-one.mp4", "/z");
        pl.AddFile("/a/other.mp4", "/a");
        pl.AddFile("/b/match-two.mp4", "/b");
        pl.SetSearch(QStringLiteral("match"));
        QCOMPARE(pl.QmlEntries().size(), 2);

        pl.toggleSortByName();
        QCOMPARE(pl.QmlEntries().size(), 2);

        pl.ToggleShuffle();
        QCOMPARE(pl.QmlEntries().size(), 2);
        pl.activateIndex(0);
        QVERIFY(pl.QmlCurrentIndex() >= 0);
        QVERIFY(pl.QmlEntries()[pl.QmlCurrentIndex()].toMap().value("current").toBool());
    }

    void OpenFileScansDirectoryForVideosOnly()
    {
        const TempDir dir({"a.mp4", "b.mkv", "c.txt", "readme"});

        Playlist pl; // no ctx -> uses default extension lists; watcher arming is skipped
        QSignalSpy scanSpy(&pl, &Playlist::scanStateChanged);
        QVERIFY(!pl.IsScanning());
        pl.OpenFile((dir.path / "a.mp4").string().c_str());

        // Non-mixed playlist: only the two video files are picked up (.txt/readme excluded).
        // With no event pump available, OpenFile scans synchronously and applies inline.
        QVERIFY((pl.Count()) == (2));
        QVERIFY((pl.Current()) >= (0)); // the opened file is selected after the scan applies
        QVERIFY(!pl.IsScanning());
        QCOMPARE(scanSpy.count(), 2);         // false -> true -> false
        QVERIFY(pl.IsManualReloadRequired()); // no event pump means no automatic watcher
    }

    void ManualReloadTracksDirectoryWatcherAvailability()
    {
        const TempDir dir({"a.mp4"});
        Playlist pl;
        pl.OpenFile((dir.path / "a.mp4").string().c_str());
        QVERIFY(pl.IsManualReloadRequired());

        Settings settings;
        const TempFile ini;
        ModuleContext ctx("pref/", &settings, ini.str());
        FakeEventPump eventPump;
        ctx.RegisterService<IEventPump>(&eventPump);
        pl.Install(ctx);

        pl.ApplySettings(true, 5, false, false, 5.0f, true, false);
        QVERIFY(!pl.IsManualReloadRequired());

        pl.ApplySettings(true, 5, false, false, 5.0f, false, false);
        QVERIFY(pl.IsManualReloadRequired());

        pl.ApplySettings(true, 5, false, false, 5.0f, true, false);
        QVERIFY(!pl.IsManualReloadRequired());
        std::error_code ec;
        std::filesystem::remove_all(dir.path, ec);
        pl.ApplySettings(true, 5, false, false, 5.0f, true, false);
        QVERIFY(pl.IsManualReloadRequired());
    }

    void ReloadUsesTheScanLifecycle()
    {
        const TempDir dir({"a.mp4"});

        Playlist pl;
        pl.OpenFile((dir.path / "a.mp4").string().c_str());
        pl.SetSearch(QStringLiteral("b.mkv"));
        QVERIFY(pl.QmlEntries().isEmpty());

        QSignalSpy scanSpy(&pl, &Playlist::scanStateChanged);
        std::ofstream(dir.path / "b.mkv") << "x";
        pl.Reload();

        QCOMPARE(pl.Count(), 2);
        QCOMPARE(pl.QmlEntries().size(), 1);
        QCOMPARE(RowPath(pl.QmlEntries(), 0), QString::fromStdString((dir.path / "b.mkv").string()));
        QVERIFY(!pl.IsScanning());
        QCOMPARE(scanSpy.count(), 2);
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

    void CleanEofAdvancesOnceAndClearsResumePosition()
    {
        Settings settings;
        const TempFile ini;
        ModuleContext ctx("pref/", &settings, ini.str());
        FakeMediaPlayer player;
        ctx.RegisterService<IMediaPlayback>(&player);

        std::string endedPath;
        double endedPosition = -1.0;
        framelift::Subscribe<FileEndedEvent>(
            ctx,
            [&](const FileEndedEvent& e)
            {
                endedPath = e.path ? e.path : "";
                endedPosition = e.position;
            }
        );

        Playlist pl;
        pl.Install(ctx);
        pl.AddFile("/movies/a.mp4", "/");
        pl.AddFile("/movies/b.mp4", "/");
        pl.Next();
        QCOMPARE(pl.Current(), 0);
        QCOMPARE(player.loadCount, 1);

        MediaEvent eof;
        eof.type = MediaEventType::EndFile;
        eof.endReason = EndFileReason::Eof;
        pl.HandleMediaEvent(eof);

        QCOMPARE(pl.Current(), 1);
        QCOMPARE(player.loadCount, 2);
        QCOMPARE(player.loadedPath, std::string("/movies/b.mp4"));
        QCOMPARE(endedPath, std::string("/movies/a.mp4"));
        QCOMPARE(endedPosition, 0.0);
    }

    void CleanEofOnLastItemStopsWithoutWrapping()
    {
        Settings settings;
        const TempFile ini;
        ModuleContext ctx("pref/", &settings, ini.str());
        FakeMediaPlayer player;
        ctx.RegisterService<IMediaPlayback>(&player);

        int stopRequests = 0;
        framelift::Subscribe<StopPlaybackRequestEvent>(
            ctx,
            [&](const StopPlaybackRequestEvent&)
            {
                ++stopRequests;
            }
        );

        Playlist pl;
        pl.Install(ctx);
        pl.AddFile("/movies/only.mp4", "/");
        pl.Next();

        MediaEvent eof;
        eof.type = MediaEventType::EndFile;
        eof.endReason = EndFileReason::Eof;
        pl.HandleMediaEvent(eof);

        QCOMPARE(stopRequests, 1);
        QCOMPARE(pl.Current(), 0);
        QCOMPARE(player.loadCount, 1);
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
