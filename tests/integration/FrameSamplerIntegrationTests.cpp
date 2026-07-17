#include "FrameSamplerService.h"
#include "TestMediaGenerator.h"

#include <QtCore/QString>
#include <QtTest/QtTest>

#include <algorithm>
#include <cstddef>
#include <future>
#include <vector>

class FrameSamplerIntegrationTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void initTestCase()
    {
        mediaPath_ = TestMediaGenerator::EnsureTestMedia(QStringLiteral("2s-160x90-mpeg4-noaudio"));
        QVERIFY2(!mediaPath_.isEmpty(), "ffmpeg test clip generation failed");
    }

    void RejectsInvalidOpenAndNullSession()
    {
        FrameSamplerService sampler;
        QVERIFY(sampler.Open(nullptr) == nullptr);
        QVERIFY(sampler.Open("") == nullptr);
        QVERIFY(sampler.Open("/definitely/missing/framelift-video.mkv") == nullptr);
        QCOMPARE(sampler.DurationSec(nullptr), 0.0);
        QVERIFY(!sampler.NativeSize(nullptr, nullptr, nullptr));
        QVERIFY(!sampler.ReadFrameRGBA(nullptr, 0.0, 0, 0, nullptr, 0, nullptr));
        sampler.Close(nullptr);
    }

    void ReportsMetadataAndReadsNativeAndScaledFrames()
    {
        FrameSamplerService sampler;
        void* session = sampler.Open(mediaPath_.toUtf8().constData());
        QVERIFY(session != nullptr);

        int width = 0;
        int height = 0;
        QVERIFY(sampler.NativeSize(session, &width, &height));
        QCOMPARE(width, 160);
        QCOMPARE(height, 90);
        QVERIFY(sampler.DurationSec(session) > 1.8);
        QVERIFY(sampler.DurationSec(session) < 2.2);

        std::vector<unsigned char> native(static_cast<std::size_t>(width * height * 4));
        double actual = -1.0;
        QVERIFY(sampler.ReadFrameRGBA(session, 0.75, 0, 0, native.data(), static_cast<int>(native.size()), &actual));
        QVERIFY(actual >= 0.70);
        QVERIFY(actual <= 0.90);
        VerifyPixels(native);

        std::vector<unsigned char> scaled(80 * 44 * 4);
        QVERIFY(sampler.ReadFrameRGBA(session, 1.0, 80, 44, scaled.data(), static_cast<int>(scaled.size()), nullptr));
        VerifyPixels(scaled);
        sampler.Close(session);
    }

    void RejectsInvalidOutputAndClampsTimestamps()
    {
        FrameSamplerService sampler;
        void* session = sampler.Open(mediaPath_.toUtf8().constData());
        QVERIFY(session != nullptr);
        std::vector<unsigned char> pixels(160 * 90 * 4);

        QVERIFY(!sampler.ReadFrameRGBA(session, 0.0, -1, 90, pixels.data(), static_cast<int>(pixels.size()), nullptr));
        QVERIFY(!sampler.ReadFrameRGBA(session, 0.0, 160, 0, pixels.data(), static_cast<int>(pixels.size()), nullptr));
        QVERIFY(!sampler.ReadFrameRGBA(session, 0.0, 160, 90, pixels.data(), 32, nullptr));

        double actual = -1.0;
        QVERIFY(sampler.ReadFrameRGBA(session, -5.0, 160, 90, pixels.data(), static_cast<int>(pixels.size()), &actual));
        QVERIFY(actual >= 0.0);
        QVERIFY(actual < 0.10);

        QVERIFY(sampler.ReadFrameRGBA(session, 99.0, 160, 90, pixels.data(), static_cast<int>(pixels.size()), &actual));
        QVERIFY(actual > 1.5);
        QVERIFY(actual <= sampler.DurationSec(session) + 0.05);
        sampler.Close(session);
    }

    void SupportsBackwardReadsAndIndependentSessions()
    {
        FrameSamplerService sampler;
        const QByteArray path = mediaPath_.toUtf8();
        void* first = sampler.Open(path.constData());
        void* second = sampler.Open(path.constData());
        QVERIFY(first != nullptr);
        QVERIFY(second != nullptr);

        std::vector<unsigned char> firstPixels(160 * 90 * 4);
        std::vector<unsigned char> secondPixels(80 * 44 * 4);
        double firstActual = 0.0;
        double secondActual = 0.0;
        auto firstRead = std::async(
            std::launch::async,
            [&]
            {
                return sampler.ReadFrameRGBA(
                    first, 1.5, 160, 90, firstPixels.data(), static_cast<int>(firstPixels.size()), &firstActual
                );
            }
        );
        auto secondRead = std::async(
            std::launch::async,
            [&]
            {
                return sampler.ReadFrameRGBA(
                    second, 0.9, 80, 44, secondPixels.data(), static_cast<int>(secondPixels.size()), &secondActual
                );
            }
        );
        QVERIFY(firstRead.get());
        QVERIFY(secondRead.get());
        QVERIFY(firstActual > secondActual);

        QVERIFY(sampler.ReadFrameRGBA(
            first, 0.25, 160, 90, firstPixels.data(), static_cast<int>(firstPixels.size()), &firstActual
        ));
        QVERIFY(firstActual >= 0.20);
        QVERIFY(firstActual < 0.40);
        VerifyPixels(firstPixels);
        VerifyPixels(secondPixels);

        sampler.Close(first);
        sampler.Close(second);
    }

private:
    static void VerifyPixels(const std::vector<unsigned char>& pixels)
    {
        QVERIFY(!pixels.empty());
        unsigned char minRgb = 255;
        unsigned char maxRgb = 0;
        for (std::size_t i = 0; i + 3 < pixels.size(); i += 4)
        {
            minRgb = std::min({minRgb, pixels[i], pixels[i + 1], pixels[i + 2]});
            maxRgb = std::max({maxRgb, pixels[i], pixels[i + 1], pixels[i + 2]});
            QCOMPARE(pixels[i + 3], static_cast<unsigned char>(255));
        }
        QVERIFY(maxRgb > minRgb);
    }

    QString mediaPath_;
};

QTEST_GUILESS_MAIN(FrameSamplerIntegrationTest)

#include "FrameSamplerIntegrationTests.moc"
