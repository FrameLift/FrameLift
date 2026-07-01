#include <framelift/UrlUtils.h>

#include "QtTestRunner.h"

#include <QtTest/QtTest>

using framelift::IsRemoteUrl;
using framelift::UrlScheme;

class UrlUtilsTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void HttpAndHttpsAreRemote()
    {
        QVERIFY(IsRemoteUrl("http://example.com/a.mp4"));
        QVERIFY(IsRemoteUrl("https://example.com/a.m3u8"));
        QVERIFY((UrlScheme("http://example.com/a.mp4")) == ("http"));
        QVERIFY((UrlScheme("https://example.com/a.m3u8")) == ("https"));
    }

    void OtherNetworkSchemesAreRemote()
    {
        QVERIFY(IsRemoteUrl("rtsp://host/stream"));
        QVERIFY(IsRemoteUrl("rtmp://host/live"));
        QVERIFY((UrlScheme("rtsp://host/stream")) == ("rtsp"));
    }

    void DedicatedSecureSchemeIsRemoteAndNamed()
    {
        QVERIFY(IsRemoteUrl("flsec://vault/clip.enc"));
        QVERIFY((UrlScheme("flsec://vault/clip.enc")) == (framelift::kSecureStreamScheme));
    }

    void SchemeIsLowercased()
    {
        QVERIFY((UrlScheme("HTTPS://Example.com")) == ("https"));
        QVERIFY(IsRemoteUrl("HtTp://x"));
    }

    void LocalPathsAreNotRemote()
    {
        QVERIFY(!(IsRemoteUrl("C:\\Users\\me\\movie.mkv")));
        QVERIFY(!(IsRemoteUrl("C:/Users/me/movie.mkv")));
        QVERIFY(!(IsRemoteUrl("/home/me/movie.mkv")));
        QVERIFY(!(IsRemoteUrl("movie.mp4")));
        QVERIFY(!(IsRemoteUrl("./rel/path.mp4")));
        QVERIFY((UrlScheme("C:\\Users\\me\\movie.mkv")) == (""));
    }

    void EdgeCases()
    {
        QVERIFY(!(IsRemoteUrl(nullptr)));
        QVERIFY(!(IsRemoteUrl("")));
        QVERIFY(!(IsRemoteUrl("://no-scheme")));       // empty scheme
        QVERIFY(!(IsRemoteUrl("weird path://thing"))); // space invalidates the scheme
    }
};

namespace
{
const ::framelift::test::Registrar<UrlUtilsTest> kRegisterUrlUtilsTest{"UrlUtilsTest"};
}

#include "UrlUtilsTests.moc"
