#include "RemoteStream.h"

#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

namespace
{
// Must match RemoteStream.cpp's reference cipher.
constexpr unsigned char kRefKey[] = {0x5A, 0xA5, 0x3C, 0xC3};

std::string Xor(const std::string& in)
{
    std::string out = in;
    for (std::size_t i = 0; i < out.size(); ++i)
    {
        out[i] = static_cast<char>(static_cast<unsigned char>(out[i]) ^ kRefKey[i % sizeof(kRefKey)]);
    }
    return out;
}

std::string ReadFile(const std::filesystem::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
} // namespace

TEST(RemoteStreamTest, PlainSchemesPassThroughUnchanged)
{
    RemoteStream rs;
    EXPECT_EQ(rs.ResolveStream("http://example.com/a.mp4"), "http://example.com/a.mp4");
    EXPECT_EQ(rs.ResolveStream("https://example.com/a.m3u8"), "https://example.com/a.m3u8");
    EXPECT_EQ(rs.ResolveStream("rtsp://host/stream"), "rtsp://host/stream");
}

TEST(RemoteStreamTest, NonUrlReturnsEmpty)
{
    RemoteStream rs;
    EXPECT_EQ(rs.ResolveStream("C:/local/file.mkv"), "");
    EXPECT_EQ(rs.ResolveStream("relative.mp4"), "");
}

TEST(RemoteStreamTest, SecureSchemeDecryptsToPlayableTempFile)
{
    const std::string plaintext = "FAKE-MEDIA-CONTENTS-0123456789-abcdefghij";

    const std::filesystem::path enc =
        std::filesystem::temp_directory_path() / "framelift_remotestream_test.enc";
    {
        std::ofstream out(enc, std::ios::binary | std::ios::trunc);
        const std::string cipher = Xor(plaintext);
        out.write(cipher.data(), static_cast<std::streamsize>(cipher.size()));
    }

    RemoteStream rs;
    const std::string resolved = rs.ResolveStream("flsec://" + enc.string());

    ASSERT_FALSE(resolved.empty());
    EXPECT_EQ(ReadFile(resolved), plaintext); // decrypted bytes match the original

    std::error_code ec;
    std::filesystem::remove(enc, ec);
    std::filesystem::remove(resolved, ec);
}

TEST(RemoteStreamTest, SecureSchemeMissingSourceFails)
{
    RemoteStream rs;
    EXPECT_EQ(rs.ResolveStream("flsec://this/path/does/not/exist.enc"), "");
}
