#include <framelift/UrlUtils.h>

#include <gtest/gtest.h>

using framelift::IsRemoteUrl;
using framelift::UrlScheme;

TEST(UrlUtilsTest, HttpAndHttpsAreRemote)
{
    EXPECT_TRUE(IsRemoteUrl("http://example.com/a.mp4"));
    EXPECT_TRUE(IsRemoteUrl("https://example.com/a.m3u8"));
    EXPECT_EQ(UrlScheme("http://example.com/a.mp4"), "http");
    EXPECT_EQ(UrlScheme("https://example.com/a.m3u8"), "https");
}

TEST(UrlUtilsTest, OtherNetworkSchemesAreRemote)
{
    EXPECT_TRUE(IsRemoteUrl("rtsp://host/stream"));
    EXPECT_TRUE(IsRemoteUrl("rtmp://host/live"));
    EXPECT_EQ(UrlScheme("rtsp://host/stream"), "rtsp");
}

TEST(UrlUtilsTest, DedicatedSecureSchemeIsRemoteAndNamed)
{
    EXPECT_TRUE(IsRemoteUrl("flsec://vault/clip.enc"));
    EXPECT_EQ(UrlScheme("flsec://vault/clip.enc"), framelift::kSecureStreamScheme);
}

TEST(UrlUtilsTest, SchemeIsLowercased)
{
    EXPECT_EQ(UrlScheme("HTTPS://Example.com"), "https");
    EXPECT_TRUE(IsRemoteUrl("HtTp://x"));
}

TEST(UrlUtilsTest, LocalPathsAreNotRemote)
{
    EXPECT_FALSE(IsRemoteUrl("C:\\Users\\me\\movie.mkv"));
    EXPECT_FALSE(IsRemoteUrl("C:/Users/me/movie.mkv"));
    EXPECT_FALSE(IsRemoteUrl("/home/me/movie.mkv"));
    EXPECT_FALSE(IsRemoteUrl("movie.mp4"));
    EXPECT_FALSE(IsRemoteUrl("./rel/path.mp4"));
    EXPECT_EQ(UrlScheme("C:\\Users\\me\\movie.mkv"), "");
}

TEST(UrlUtilsTest, EdgeCases)
{
    EXPECT_FALSE(IsRemoteUrl(nullptr));
    EXPECT_FALSE(IsRemoteUrl(""));
    EXPECT_FALSE(IsRemoteUrl("://no-scheme"));      // empty scheme
    EXPECT_FALSE(IsRemoteUrl("weird path://thing")); // space invalidates the scheme
}
