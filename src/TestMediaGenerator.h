#pragma once

#include <QtCore/QString>
#include <QtCore/QStringList>

struct TestMediaSpec;

// Backs the FL_TEST_FILE launch flag: turns a parsed TestMediaSpec (see
// TestMediaSpec.h) into a synthetic clip by running the external `ffmpeg`
// binary with lavfi sources, caching the result under the user cache dir so
// repeat launches are instant.

namespace TestMediaGenerator
{

// Parse `spec` and return the cached clip path, generating it on a cache miss.
// Returns "" on any failure (bad spec, ffmpeg missing, encode error) — the
// cause is already logged, and the caller should start without a file.
QString EnsureTestMedia(const QString& spec);

// Pure argument construction, split out for unit tests. `srtPath` is empty
// when spec.subtitles == TestSubtitleKind::None.
QStringList BuildFfmpegArgs(const TestMediaSpec& spec, const QString& outPath, const QString& srtPath);

} // namespace TestMediaGenerator
