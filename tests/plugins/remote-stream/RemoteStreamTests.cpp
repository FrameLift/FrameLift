#include "RemoteStream.h"

#include <filesystem>
#include <fstream>
#include <string>

#include "QtTestRunner.h"

#include <QtTest/QtTest>

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

class RemoteStreamTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void PlainSchemesPassThroughUnchanged()
    {
        RemoteStream rs;
        QVERIFY((rs.ResolveStream("http://example.com/a.mp4")) == ("http://example.com/a.mp4"));
        QVERIFY((rs.ResolveStream("https://example.com/a.m3u8")) == ("https://example.com/a.m3u8"));
        QVERIFY((rs.ResolveStream("rtsp://host/stream")) == ("rtsp://host/stream"));
    }

    void NonUrlReturnsEmpty()
    {
        RemoteStream rs;
        QVERIFY((rs.ResolveStream("C:/local/file.mkv")) == (""));
        QVERIFY((rs.ResolveStream("relative.mp4")) == (""));
    }

    void SecureSchemeDecryptsToPlayableTempFile()
    {
        const std::string plaintext = "FAKE-MEDIA-CONTENTS-0123456789-abcdefghij";

        const std::filesystem::path enc = std::filesystem::temp_directory_path() / "framelift_remotestream_test.enc";
        {
            std::ofstream out(enc, std::ios::binary | std::ios::trunc);
            const std::string cipher = Xor(plaintext);
            out.write(cipher.data(), static_cast<std::streamsize>(cipher.size()));
        }

        RemoteStream rs;
        const std::string resolved = rs.ResolveStream("flsec://" + enc.string());

        QVERIFY(!(resolved.empty()));
        QVERIFY((ReadFile(resolved)) == (plaintext)); // decrypted bytes match the original

        std::error_code ec;
        std::filesystem::remove(enc, ec);
        std::filesystem::remove(resolved, ec);
    }

    void SecureSchemeMissingSourceFails()
    {
        RemoteStream rs;
        QVERIFY((rs.ResolveStream("flsec://this/path/does/not/exist.enc")) == (""));
    }
};

namespace
{
const ::framelift::test::Registrar<RemoteStreamTest> kRegisterRemoteStreamTest{"RemoteStreamTest"};
}

#include "RemoteStreamTests.moc"
