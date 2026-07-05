// Unit tests for VideoFrameGate — the decode→render latest-wins frame mailbox.
// The opaque channel is exercised with a counting deleter standing in for
// av_frame_free; the header is libav-free so it builds in the standalone suite.

#include "VideoFrameGate.h"

#include "QtTestRunner.h"

#include <QtTest/QtTest>

#include <vector>

namespace
{
int gReleased = 0;

void CountingRelease(void* /*p*/)
{
    ++gReleased;
}

// Opaque handles only need distinct addresses.
int gTokenA = 0;
int gTokenB = 0;

// Tight desc for a small RGBA frame (the pixels channel's most common shape).
VideoFrameDesc RgbaDesc(int w, int h)
{
    VideoFrameDesc d;
    d.format = VideoPixelFormat::RGBA;
    d.w = w;
    d.h = h;
    FillTightLayout(d);
    return d;
}
} // namespace

class VideoFrameGateTests final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void PixelsRoundTrip()
    {
        VideoFrameGate gate;
        QVERIFY(!gate.HasNewFrame());

        std::vector<uint8_t> buf{1, 2, 3, 4};
        gate.PublishPixels(buf, RgbaDesc(2, 1));
        QVERIFY(gate.HasNewFrame());

        const auto acq = gate.Acquire();
        QVERIFY(acq.newPixels);
        QVERIFY(!acq.newOpaque);
        QCOMPARE(acq.w, 2);
        QCOMPARE(acq.h, 1);
        QCOMPARE(gate.DisplayPixels(), (std::vector<uint8_t>{1, 2, 3, 4}));
        QVERIFY(!gate.HasNewFrame());

        // Nothing pending: a second acquire is a no-op that keeps the display frame.
        const auto again = gate.Acquire();
        QVERIFY(!again.newPixels && !again.newOpaque);
        QCOMPARE(gate.DisplayPixels(), (std::vector<uint8_t>{1, 2, 3, 4}));
    }

    void PixelsLatestWinsAndRecyclesBuffers()
    {
        VideoFrameGate gate;
        std::vector<uint8_t> first{1};
        std::vector<uint8_t> second{2};
        gate.PublishPixels(first, RgbaDesc(1, 1));
        gate.PublishPixels(second, RgbaDesc(1, 1)); // unconsumed pending is handed back to the producer
        QCOMPARE(second, (std::vector<uint8_t>{1}));

        const auto acq = gate.Acquire();
        QVERIFY(acq.newPixels);
        QCOMPARE(gate.DisplayPixels(), (std::vector<uint8_t>{2}));
    }

    void DescRoundTripsAndLatestWins()
    {
        VideoFrameGate gate;
        VideoFrameDesc nv12;
        nv12.format = VideoPixelFormat::NV12;
        nv12.w = 4;
        nv12.h = 2;
        nv12.colorspace = 1; // BT.709
        nv12.fullRange = 1;
        FillTightLayout(nv12);

        std::vector<uint8_t> a(RequiredBytes(nv12), 7);
        gate.PublishPixels(a, RgbaDesc(4, 2)); // stale RGBA pending…
        std::vector<uint8_t> b(RequiredBytes(nv12), 8);
        gate.PublishPixels(b, nv12); // …replaced by the NV12 frame (latest wins)

        const auto acq = gate.Acquire();
        QVERIFY(acq.newPixels);
        QVERIFY(acq.desc.format == VideoPixelFormat::NV12);
        QCOMPARE(acq.desc.colorspace, 1);
        QCOMPARE(acq.desc.fullRange, 1);
        QCOMPARE(acq.desc.planeOffset[1], static_cast<size_t>(8));
        QVERIFY(gate.DisplayDesc().format == VideoPixelFormat::NV12);
        QCOMPARE(gate.DisplayPixels().size(), RequiredBytes(nv12));
    }

    void OpaqueAdoptReleasesPreviousDisplay()
    {
        gReleased = 0;
        VideoFrameGate gate;
        gate.SetOpaqueRelease(&CountingRelease);

        gate.PublishOpaque(&gTokenA, 4, 2);
        auto acq = gate.Acquire();
        QVERIFY(acq.newOpaque);
        QCOMPARE(acq.w, 4);
        QCOMPARE(gate.DisplayOpaque(), static_cast<void*>(&gTokenA));
        QCOMPARE(gReleased, 0);

        gate.PublishOpaque(&gTokenB, 4, 2);
        acq = gate.Acquire();
        QVERIFY(acq.newOpaque);
        QCOMPARE(gate.DisplayOpaque(), static_cast<void*>(&gTokenB));
        QCOMPARE(gReleased, 1); // the displayed A was dropped on adopt
    }

    void OpaqueLatestWinsReleasesUnconsumedPending()
    {
        gReleased = 0;
        VideoFrameGate gate;
        gate.SetOpaqueRelease(&CountingRelease);
        gate.PublishOpaque(&gTokenA, 1, 1);
        gate.PublishOpaque(&gTokenB, 1, 1); // A never displayed → released now
        QCOMPARE(gReleased, 1);
        const auto acq = gate.Acquire();
        QVERIFY(acq.newOpaque);
        QCOMPARE(gate.DisplayOpaque(), static_cast<void*>(&gTokenB));
    }

    void ChannelSwitchToPixelsDropsHeldOpaque()
    {
        gReleased = 0;
        VideoFrameGate gate;
        gate.SetOpaqueRelease(&CountingRelease);

        gate.PublishOpaque(&gTokenA, 1, 1);
        auto acq = gate.Acquire();
        gate.CommitDisplayChannel(acq);
        QVERIFY(gate.DisplayIsOpaque());

        // A software-decoded file follows: first pixels frame flips the channel
        // and releases the held opaque frame so its pool/device can go away.
        std::vector<uint8_t> buf{9};
        gate.PublishPixels(buf, RgbaDesc(1, 1));
        acq = gate.Acquire();
        gate.CommitDisplayChannel(acq);
        QVERIFY(!gate.DisplayIsOpaque());
        QCOMPARE(gate.DisplayOpaque(), static_cast<void*>(nullptr));
        QCOMPARE(gReleased, 1);
    }

    void CommitWithoutNewFrameKeepsChannel()
    {
        gReleased = 0;
        VideoFrameGate gate;
        gate.SetOpaqueRelease(&CountingRelease);
        gate.PublishOpaque(&gTokenA, 1, 1);
        auto acq = gate.Acquire();
        gate.CommitDisplayChannel(acq);
        QVERIFY(gate.DisplayIsOpaque());

        acq = gate.Acquire(); // nothing pending
        gate.CommitDisplayChannel(acq);
        QVERIFY(gate.DisplayIsOpaque()); // unchanged, frame still held
        QCOMPARE(gReleased, 0);
    }

    void ReleaseAllFreesPendingAndDisplayed()
    {
        gReleased = 0;
        VideoFrameGate gate;
        gate.SetOpaqueRelease(&CountingRelease);
        gate.PublishOpaque(&gTokenA, 1, 1);
        (void)gate.Acquire();
        gate.PublishOpaque(&gTokenB, 1, 1); // B pending, A displayed
        gate.ReleaseAll();
        QCOMPARE(gReleased, 2);
        QCOMPARE(gate.DisplayOpaque(), static_cast<void*>(nullptr));
    }

    void DestructorReleasesHeldFrames()
    {
        gReleased = 0;
        {
            VideoFrameGate gate;
            gate.SetOpaqueRelease(&CountingRelease);
            gate.PublishOpaque(&gTokenA, 1, 1);
            (void)gate.Acquire();
        }
        QCOMPARE(gReleased, 1);
    }
};

namespace
{
const ::framelift::test::Registrar<VideoFrameGateTests> kRegisterVideoFrameGateTests{"VideoFrameGateTests"};
}

#include "VideoFrameGateTests.moc"
