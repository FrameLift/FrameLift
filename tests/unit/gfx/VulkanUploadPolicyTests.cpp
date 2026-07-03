#include "VulkanUploadPolicy.h"

#include "QtTestRunner.h"

#include <QtTest/QtTest>

using namespace VulkanUploadPolicy;

class VulkanUploadPolicyTests final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void FirstAllocationUsesTheFloor()
    {
        QCOMPARE(NextStagingArenaSize(0, 1024), kMinStagingArena);
    }

    void GrowthAtLeastDoubles()
    {
        const uint64_t size = 8ull << 20;
        QCOMPARE(NextStagingArenaSize(size, size + 1), size * 2);
    }

    void HugeNeedWinsOverDoubling()
    {
        const uint64_t need = 64ull << 20;
        QCOMPARE(NextStagingArenaSize(4ull << 20, need), need);
    }

    void OffsetsAlignTo16()
    {
        QCOMPARE(AlignArenaOffset(0), uint64_t{0});
        QCOMPARE(AlignArenaOffset(1), uint64_t{16});
        QCOMPARE(AlignArenaOffset(16), uint64_t{16});
        QCOMPARE(AlignArenaOffset(17), uint64_t{32});
    }
};

namespace
{
const ::framelift::test::Registrar<VulkanUploadPolicyTests> kRegisterVulkanUploadPolicyTests{"VulkanUploadPolicyTests"};
}

#include "VulkanUploadPolicyTests.moc"
