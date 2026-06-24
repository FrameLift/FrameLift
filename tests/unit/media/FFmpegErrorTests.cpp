// Unit tests for the FFmpeg backend's pure AVERROR → EndFileReason classification
// (issue #13). FFmpegError.h is deliberately free of libav so it builds in the
// standalone native suite; these tests feed the raw integer error codes FFmpeg returns.

#include "FFmpegError.h"

#include <gtest/gtest.h>

TEST(FFmpegErrorTests, MissingOrInaccessibleFileIsNotFound)
{
    EXPECT_EQ(ClassifyAvError(kAvErrNoEnt), EndFileReason::NotFound);
    EXPECT_EQ(ClassifyAvError(kAvErrAccess), EndFileReason::NotFound);
}

TEST(FFmpegErrorTests, MissingDemuxerDecoderProtocolBsfIsUnsupported)
{
    EXPECT_EQ(ClassifyAvError(kAvErrDemuxerNotFound), EndFileReason::Unsupported);
    EXPECT_EQ(ClassifyAvError(kAvErrDecoderNotFound), EndFileReason::Unsupported);
    EXPECT_EQ(ClassifyAvError(kAvErrProtocolNotFound), EndFileReason::Unsupported);
    EXPECT_EQ(ClassifyAvError(kAvErrBsfNotFound), EndFileReason::Unsupported);
}

TEST(FFmpegErrorTests, InvalidOrTruncatedDataIsCorrupt)
{
    EXPECT_EQ(ClassifyAvError(kAvErrInvalidData), EndFileReason::Corrupt);
    EXPECT_EQ(ClassifyAvError(kAvErrEof), EndFileReason::Corrupt);
}

TEST(FFmpegErrorTests, StreamNotFoundIsNoStream)
{
    EXPECT_EQ(ClassifyAvError(kAvErrStreamNotFound), EndFileReason::NoStream);
}

TEST(FFmpegErrorTests, UnrecognisedCodeFallsBackToError)
{
    // An arbitrary code we don't classify must not regress to a misleading reason.
    EXPECT_EQ(ClassifyAvError(-9999), EndFileReason::Error);
    EXPECT_EQ(ClassifyAvError(0), EndFileReason::Error);
}

// The mirrored constants are FFERRTAG-computed; pin a couple of well-known values so a
// refactor of the MkTag/FfErrTag helpers can't silently change them. (FfErrTag negates
// MKTAG; e.g. AVERROR_EOF == -('E' | 'O'<<8 | 'F'<<16 | ' '<<24).)
TEST(FFmpegErrorTests, MirroredTagsMatchFFmpegFormula)
{
    const int eof = -static_cast<int>('E' | ('O' << 8) | ('F' << 16) | (static_cast<unsigned>(' ') << 24));
    EXPECT_EQ(kAvErrEof, eof);

    const int indata = -static_cast<int>('I' | ('N' << 8) | ('D' << 16) | (static_cast<unsigned>('A') << 24));
    EXPECT_EQ(kAvErrInvalidData, indata);
}
