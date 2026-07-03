#include "TestMediaGenerator.h"

#include "TestMediaSpec.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QProcess>
#include <QtCore/QStandardPaths>

#include <framelift/Log.h>

namespace TestMediaGenerator
{

namespace
{

// Free-form lavfi specs and the temp-suffixed output hide nothing subtle: the
// only non-obvious flags are `-f matroska` (the `.part` suffix defeats
// extension-based muxer inference) and `-pix_fmt yuv420p` (broadest decoder
// compatibility; also what real-world files overwhelmingly use).
QString EncoderFor(const std::string& codec)
{
    if (codec == "h264")
    {
        return QStringLiteral("libx264");
    }
    if (codec == "hevc")
    {
        return QStringLiteral("libx265");
    }
    if (codec == "vp9")
    {
        return QStringLiteral("libvpx-vp9");
    }
    if (codec == "av1")
    {
        return QStringLiteral("libaom-av1");
    }
    return QStringLiteral("mpeg4");
}

} // namespace

QStringList BuildFfmpegArgs(const TestMediaSpec& spec, const QString& outPath, const QString& srtPath)
{
    QStringList args{QStringLiteral("-y"), QStringLiteral("-loglevel"), QStringLiteral("error")};

    args << QStringLiteral("-f") << QStringLiteral("lavfi") << QStringLiteral("-i")
         << QStringLiteral("testsrc2=size=%1x%2:rate=30:duration=%3")
                .arg(spec.width)
                .arg(spec.height)
                .arg(spec.durationSeconds);

    int subtitleInput = 1;
    if (spec.audio)
    {
        args << QStringLiteral("-f") << QStringLiteral("lavfi") << QStringLiteral("-i")
             << QStringLiteral("sine=frequency=440:sample_rate=48000:duration=%1").arg(spec.durationSeconds);
        subtitleInput = 2;
    }
    if (!srtPath.isEmpty())
    {
        args << QStringLiteral("-i") << srtPath;
    }

    args << QStringLiteral("-map") << QStringLiteral("0:v");
    if (spec.audio)
    {
        args << QStringLiteral("-map") << QStringLiteral("1:a");
    }
    if (!srtPath.isEmpty())
    {
        args << QStringLiteral("-map") << QStringLiteral("%1:s").arg(subtitleInput);
    }

    args << QStringLiteral("-c:v") << EncoderFor(spec.codec) << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p");
    if (spec.audio)
    {
        args << QStringLiteral("-c:a") << QStringLiteral("aac");
    }
    if (!srtPath.isEmpty())
    {
        args << QStringLiteral("-c:s")
             << (spec.subtitles == TestSubtitleKind::Ass ? QStringLiteral("ass") : QStringLiteral("srt"));
    }

    args << QStringLiteral("-f") << QStringLiteral("matroska") << outPath;
    return args;
}

QString EnsureTestMedia(const QString& spec)
{
    const auto parsed = ParseTestMediaSpec(spec.toStdString());
    if (!parsed)
    {
        Log::Error(
            "FL_TEST_FILE: unparseable spec '{}' (expected tokens like 30s-1080p-h264-srt, see docs/env-flags.md)",
            spec.toStdString()
        );
        return {};
    }

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/test-media";
    if (!QDir().mkpath(dir))
    {
        Log::Error("FL_TEST_FILE: cannot create cache dir {}", dir.toStdString());
        return {};
    }

    const QString stem = dir + "/" + QString::fromStdString(CanonicalTestMediaName(*parsed));
    const QString outPath = stem + ".mkv";
    if (QFile::exists(outPath))
    {
        Log::Info("FL_TEST_FILE: cache hit {}", outPath.toStdString());
        return outPath;
    }

    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty())
    {
        Log::Error("FL_TEST_FILE: ffmpeg not found in PATH; skipping test media generation");
        return {};
    }

    // Subtitle cues are always authored as SubRip; ffmpeg transcodes to ASS at
    // mux time when the spec asks for it.
    QString srtPath;
    if (parsed->subtitles != TestSubtitleKind::None)
    {
        srtPath = stem + ".srt.tmp";
        QFile srt(srtPath);
        if (!srt.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            Log::Error("FL_TEST_FILE: cannot write subtitle file {}", srtPath.toStdString());
            return {};
        }
        const std::string cues = BuildTestSubtitleCues(parsed->durationSeconds);
        srt.write(cues.data(), static_cast<qint64>(cues.size()));
        srt.close();
    }

    // Encode to a .part file and rename on success so an interrupted or failed
    // run never leaves a truncated clip in the cache.
    const QString partPath = outPath + ".part";
    Log::Info("FL_TEST_FILE: generating {} via {}", outPath.toStdString(), ffmpeg.toStdString());

    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(ffmpeg, BuildFfmpegArgs(*parsed, partPath, srtPath));
    constexpr int kTimeoutMs = 120'000; // bounds slow encoders (av1/hevc at 4k)
    const bool finished = proc.waitForFinished(kTimeoutMs);
    if (!finished)
    {
        proc.kill();
        proc.waitForFinished(5'000);
    }

    if (!srtPath.isEmpty())
    {
        QFile::remove(srtPath);
    }

    if (!finished || proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
    {
        const QString output = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
        const std::string cause = finished ? "exit " + std::to_string(proc.exitCode()) : std::string("timeout");
        Log::Error("FL_TEST_FILE: ffmpeg failed ({}): {}", cause, output.toStdString());
        QFile::remove(partPath);
        return {};
    }

    if (!QFile::rename(partPath, outPath))
    {
        Log::Error("FL_TEST_FILE: cannot move {} into place", partPath.toStdString());
        QFile::remove(partPath);
        return {};
    }
    return outPath;
}

} // namespace TestMediaGenerator
