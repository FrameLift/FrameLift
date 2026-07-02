// Unit tests for ScanSidecarFiles — the fuzzy sidecar subtitle/audio discovery
// next to a media file. Pure std::filesystem, exercised against a QTemporaryDir.

#include "FFmpegSidecarScan.h"

#include "QtTestRunner.h"

#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

namespace
{
void Touch(const std::filesystem::path& p)
{
    std::ofstream out(p);
    out << "x";
}

bool Contains(const std::vector<ExternalSource>& found, const std::string& filename, bool isAudio)
{
    return std::any_of(
        found.begin(), found.end(),
        [&](const ExternalSource& s)
        {
            return std::filesystem::path(s.path).filename().string() == filename && s.isAudio == isAudio;
        }
    );
}
} // namespace

class FFmpegSidecarScanTests final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void FindsFuzzyMatchedSidecars()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const std::filesystem::path dir(tmp.path().toStdString());
        const auto media = dir / "movie.mkv";
        Touch(media);
        Touch(dir / "movie.srt");        // exact stem
        Touch(dir / "movie.en.srt");     // fuzzy: stem + language suffix
        Touch(dir / "movie.mka");        // audio sidecar
        Touch(dir / "other.srt");        // different stem → ignored
        Touch(dir / "movie.txt");        // unknown extension → ignored
        Touch(dir / "MOVIE.commentary.flac"); // case-insensitive stem match

        const auto found = ScanSidecarFiles(media.string(), true, true);
        QCOMPARE(found.size(), std::size_t{4});
        QVERIFY(Contains(found, "movie.srt", false));
        QVERIFY(Contains(found, "movie.en.srt", false));
        QVERIFY(Contains(found, "movie.mka", true));
        QVERIFY(Contains(found, "MOVIE.commentary.flac", true));
    }

    void SkipsTheMediaFileItself()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const std::filesystem::path dir(tmp.path().toStdString());
        // The media file's own extension is in the audio list — it must still be skipped.
        const auto media = dir / "song.flac";
        Touch(media);
        const auto found = ScanSidecarFiles(media.string(), true, true);
        QVERIFY(found.empty());
    }

    void AutoLoadGatesFilterByKind()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const std::filesystem::path dir(tmp.path().toStdString());
        const auto media = dir / "movie.mkv";
        Touch(media);
        Touch(dir / "movie.srt");
        Touch(dir / "movie.mka");

        const auto subsOnly = ScanSidecarFiles(media.string(), true, false);
        QCOMPARE(subsOnly.size(), std::size_t{1});
        QVERIFY(Contains(subsOnly, "movie.srt", false));

        const auto audioOnly = ScanSidecarFiles(media.string(), false, true);
        QCOMPARE(audioOnly.size(), std::size_t{1});
        QVERIFY(Contains(audioOnly, "movie.mka", true));

        QVERIFY(ScanSidecarFiles(media.string(), false, false).empty());
    }

    void HandlesPathWithoutDirectory()
    {
        QVERIFY(ScanSidecarFiles("movie.mkv", true, true).empty());
        QVERIFY(ScanSidecarFiles("", true, true).empty());
    }
};

namespace
{
const ::framelift::test::Registrar<FFmpegSidecarScanTests> kRegisterFFmpegSidecarScanTests{"FFmpegSidecarScanTests"};
}

#include "FFmpegSidecarScanTests.moc"
