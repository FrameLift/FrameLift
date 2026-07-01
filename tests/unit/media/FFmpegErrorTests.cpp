// Unit tests for the FFmpeg backend's pure AVERROR → EndFileReason classification
// (issue #13). FFmpegError.h is deliberately free of libav so it builds in the
// standalone native suite; these tests feed the raw integer error codes FFmpeg returns.

#include "FFmpegError.h"

#include "QtTestRunner.h"

#include <QtTest/QtTest>

class FFmpegErrorTests final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void MissingOrInaccessibleFileIsNotFound()
    {
        QVERIFY((ClassifyAvError(kAvErrNoEnt)) == (EndFileReason::NotFound));
        QVERIFY((ClassifyAvError(kAvErrAccess)) == (EndFileReason::NotFound));
    }

    void MissingDemuxerDecoderProtocolBsfIsUnsupported()
    {
        QVERIFY((ClassifyAvError(kAvErrDemuxerNotFound)) == (EndFileReason::Unsupported));
        QVERIFY((ClassifyAvError(kAvErrDecoderNotFound)) == (EndFileReason::Unsupported));
        QVERIFY((ClassifyAvError(kAvErrProtocolNotFound)) == (EndFileReason::Unsupported));
        QVERIFY((ClassifyAvError(kAvErrBsfNotFound)) == (EndFileReason::Unsupported));
    }

    void InvalidOrTruncatedDataIsCorrupt()
    {
        QVERIFY((ClassifyAvError(kAvErrInvalidData)) == (EndFileReason::Corrupt));
        QVERIFY((ClassifyAvError(kAvErrEof)) == (EndFileReason::Corrupt));
    }

    void StreamNotFoundIsNoStream()
    {
        QVERIFY((ClassifyAvError(kAvErrStreamNotFound)) == (EndFileReason::NoStream));
    }

    void UnrecognisedCodeFallsBackToError()
    {
        // An arbitrary code we don't classify must not regress to a misleading reason.
        QVERIFY((ClassifyAvError(-9999)) == (EndFileReason::Error));
        QVERIFY((ClassifyAvError(0)) == (EndFileReason::Error));
    }

    // The mirrored constants are FFERRTAG-computed; pin a couple of well-known values so a
    // refactor of the MkTag/FfErrTag helpers can't silently change them. (FfErrTag negates
    // MKTAG; e.g. AVERROR_EOF == -('E' | 'O'<<8 | 'F'<<16 | ' '<<24).)
    void MirroredTagsMatchFFmpegFormula()
    {
        const int eof = -static_cast<int>('E' | ('O' << 8) | ('F' << 16) | (static_cast<unsigned>(' ') << 24));
        QVERIFY((kAvErrEof) == (eof));

        const int indata = -static_cast<int>('I' | ('N' << 8) | ('D' << 16) | (static_cast<unsigned>('A') << 24));
        QVERIFY((kAvErrInvalidData) == (indata));
    }
};

namespace
{
const ::framelift::test::Registrar<FFmpegErrorTests> kRegisterFFmpegErrorTests{"FFmpegErrorTests"};
}

#include "FFmpegErrorTests.moc"
