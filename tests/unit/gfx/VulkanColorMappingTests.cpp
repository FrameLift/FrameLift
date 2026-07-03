#include "VulkanColorMapping.h"

#include "QtTestRunner.h"

#include <QtTest/QtTest>

using namespace VulkanColorMapping;

class VulkanColorMappingTests final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void Bt709MapsTo709()
    {
        QCOMPARE(ModelFromAvColorSpace(1), kModelYcbcr709);
    }

    void Bt601FamilyMapsTo601()
    {
        for (int avSpace : {4, 5, 6, 7}) // FCC, BT470BG, SMPTE170M, SMPTE240M
        {
            QCOMPARE(ModelFromAvColorSpace(avSpace), kModelYcbcr601);
        }
    }

    void Bt2020MapsTo2020()
    {
        QCOMPARE(ModelFromAvColorSpace(9), kModelYcbcr2020);  // NCL
        QCOMPARE(ModelFromAvColorSpace(10), kModelYcbcr2020); // CL
    }

    void UnknownSpacesFallBackTo709()
    {
        for (int avSpace : {0, 2, 3, 8, 11, 42, -1}) // RGB, unspecified, reserved, …
        {
            QCOMPARE(ModelFromAvColorSpace(avSpace), kModelYcbcr709);
        }
    }

    void OnlyJpegRangeIsFull()
    {
        QCOMPARE(RangeFromAvColorRange(2), kRangeItuFull); // AVCOL_RANGE_JPEG
        for (int avRange : {0, 1, 3, -1})                  // unspecified, MPEG, out-of-range
        {
            QCOMPARE(RangeFromAvColorRange(avRange), kRangeItuNarrow);
        }
    }
};

namespace
{
const ::framelift::test::Registrar<VulkanColorMappingTests> kRegisterVulkanColorMappingTests{"VulkanColorMappingTests"};
}

#include "VulkanColorMappingTests.moc"
