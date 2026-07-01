#include "ExtensionFilter.h"

#include "QtTestRunner.h"

#include <QtTest/QtTest>

class ExtensionFilterTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void MatchesListedExtension()
    {
        QVERIFY(ExtensionInList("video.mp4", "mp4;mkv;webm"));
        QVERIFY(ExtensionInList("clip.mkv", "mp4;mkv;webm"));   // middle
        QVERIFY(ExtensionInList("movie.webm", "mp4;mkv;webm")); // last
    }

    void IsCaseInsensitive()
    {
        QVERIFY(ExtensionInList("VIDEO.MP4", "mp4;mkv"));
        QVERIFY(ExtensionInList("Photo.PnG", "png;jpg"));
    }

    void RejectsUnlistedExtension()
    {
        QVERIFY(!(ExtensionInList("image.png", "mp4;mkv")));
        QVERIFY(!(ExtensionInList("archive.zip", "mp4;mkv;webm")));
    }

    void HandlesPathsWithDirectories()
    {
        QVERIFY(ExtensionInList("/home/user/My Videos/clip.mp4", "mp4"));
    }

    void NoExtensionDoesNotMatch()
    {
        QVERIFY(!(ExtensionInList("README", "mp4;mkv")));
    }

    void EmptyListMatchesNothing()
    {
        QVERIFY(!(ExtensionInList("video.mp4", "")));
    }

    void DoesNotPartialMatch()
    {
        // "mp4" must not match "mp" or vice versa.
        QVERIFY(!(ExtensionInList("video.mp", "mp4")));
        QVERIFY(!(ExtensionInList("video.mp4", "mp")));
    }
};

namespace
{
const ::framelift::test::Registrar<ExtensionFilterTest> kRegisterExtensionFilterTest{"ExtensionFilterTest"};
}

#include "ExtensionFilterTests.moc"
