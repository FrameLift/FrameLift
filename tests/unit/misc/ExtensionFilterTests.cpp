#include "ExtensionFilter.h"

#include <gtest/gtest.h>

TEST(ExtensionFilterTest, MatchesListedExtension)
{
    EXPECT_TRUE(ExtensionInList("video.mp4", "mp4;mkv;webm"));
    EXPECT_TRUE(ExtensionInList("clip.mkv", "mp4;mkv;webm"));   // middle
    EXPECT_TRUE(ExtensionInList("movie.webm", "mp4;mkv;webm")); // last
}

TEST(ExtensionFilterTest, IsCaseInsensitive)
{
    EXPECT_TRUE(ExtensionInList("VIDEO.MP4", "mp4;mkv"));
    EXPECT_TRUE(ExtensionInList("Photo.PnG", "png;jpg"));
}

TEST(ExtensionFilterTest, RejectsUnlistedExtension)
{
    EXPECT_FALSE(ExtensionInList("image.png", "mp4;mkv"));
    EXPECT_FALSE(ExtensionInList("archive.zip", "mp4;mkv;webm"));
}

TEST(ExtensionFilterTest, HandlesPathsWithDirectories)
{
    EXPECT_TRUE(ExtensionInList("/home/user/My Videos/clip.mp4", "mp4"));
}

TEST(ExtensionFilterTest, NoExtensionDoesNotMatch)
{
    EXPECT_FALSE(ExtensionInList("README", "mp4;mkv"));
}

TEST(ExtensionFilterTest, EmptyListMatchesNothing)
{
    EXPECT_FALSE(ExtensionInList("video.mp4", ""));
}

TEST(ExtensionFilterTest, DoesNotPartialMatch)
{
    // "mp4" must not match "mp" or vice versa.
    EXPECT_FALSE(ExtensionInList("video.mp", "mp4"));
    EXPECT_FALSE(ExtensionInList("video.mp4", "mp"));
}
