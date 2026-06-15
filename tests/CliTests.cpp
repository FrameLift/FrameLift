#include "Cli.h"

#include <gtest/gtest.h>

namespace
{
// argv arrays are NUL-terminated const char* lists, with argv[0] = program name.
std::string Parse(const std::initializer_list<const char*> args)
{
    return ParseOpenTarget(static_cast<int>(args.size()), args.begin());
}
} // namespace

TEST(CliTest, ReturnsLocalPath)
{
    EXPECT_EQ(Parse({"FrameLift", "movie.mkv"}), "movie.mkv");
}

TEST(CliTest, ReturnsUrl)
{
    EXPECT_EQ(Parse({"FrameLift", "https://example.com/stream.m3u8"}), "https://example.com/stream.m3u8");
}

TEST(CliTest, EmptyWhenNoArgs)
{
    EXPECT_EQ(Parse({"FrameLift"}), "");
}

TEST(CliTest, SkipsLeadingFlags)
{
    EXPECT_EQ(Parse({"FrameLift", "--verbose", "-x", "movie.mkv"}), "movie.mkv");
}

TEST(CliTest, EmptyWhenOnlyFlags)
{
    EXPECT_EQ(Parse({"FrameLift", "--help", "-v"}), "");
}

TEST(CliTest, FirstPositionalWinsOverLaterArgs)
{
    EXPECT_EQ(Parse({"FrameLift", "first.mkv", "second.mkv"}), "first.mkv");
}

TEST(CliTest, HandlesNullArgvAndZeroArgc)
{
    EXPECT_EQ(ParseOpenTarget(0, nullptr), "");
    EXPECT_EQ(ParseOpenTarget(5, nullptr), "");
}
