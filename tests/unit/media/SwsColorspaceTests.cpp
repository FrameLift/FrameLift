#include "SwsColorspace.h"

#include "QtTestRunner.h"

#include <QtTest/QtTest>

using namespace SwsColorspace;

class SwsColorspaceTests final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void TaggedColorspacePassesThrough()
    {
        // A frame that carries a real colorspace tag keeps it (height is ignored).
        QCOMPARE(ResolveCoefficients(1, 480), 1);  // BT.709 on an SD frame
        QCOMPARE(ResolveCoefficients(5, 1080), 5); // BT.601 (BT470BG) on an HD frame
        QCOMPARE(ResolveCoefficients(6, 1080), 6); // SMPTE170M
        QCOMPARE(ResolveCoefficients(7, 240), 7);  // SMPTE240M
    }

    void UnspecifiedResolvesByHeight()
    {
        for (int untagged : {kAvColSpcUnspecified, kAvColSpcRgb, -1})
        {
            QCOMPARE(ResolveCoefficients(untagged, 1080), kAvColSpcBt709); // HD → 709
            QCOMPARE(ResolveCoefficients(untagged, 720), kAvColSpcBt709);  // HD → 709
            QCOMPARE(ResolveCoefficients(untagged, 576), kAvColSpc601);    // SD → 601
            QCOMPARE(ResolveCoefficients(untagged, 480), kAvColSpc601);    // SD → 601
        }
    }

    void HeightBoundaryIs576()
    {
        // 576 is SD (PAL), 577+ is HD.
        QCOMPARE(ResolveCoefficients(kAvColSpcUnspecified, 576), kAvColSpc601);
        QCOMPARE(ResolveCoefficients(kAvColSpcUnspecified, 577), kAvColSpcBt709);
    }

    void OnlyJpegRangeIsFull()
    {
        QCOMPARE(FullRange(kAvColRangeJpeg), 1); // AVCOL_RANGE_JPEG → full
        for (int range : {0, 1, 3, -1})          // unspecified, MPEG, out-of-range
        {
            QCOMPARE(FullRange(range), 0);
        }
    }
};

namespace
{
const ::framelift::test::Registrar<SwsColorspaceTests> kRegisterSwsColorspaceTests{"SwsColorspaceTests"};
}

#include "SwsColorspaceTests.moc"
