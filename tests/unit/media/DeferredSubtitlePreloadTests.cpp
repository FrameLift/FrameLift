#include "DeferredSubtitlePreload.h"

#include "QtTestRunner.h"

#include <QtTest/QtTest>

#include <cstddef>
#include <vector>

class DeferredSubtitlePreloadTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void CueBatchEndBoundsEachBatch()
    {
        struct Case
        {
            std::size_t cueCount;
            std::vector<std::size_t> expectedSizes;
        };

        const std::vector<Case> cases{
            {DeferredSubtitlePreload::kCueBatchSize - 1, {DeferredSubtitlePreload::kCueBatchSize - 1}},
            {DeferredSubtitlePreload::kCueBatchSize, {DeferredSubtitlePreload::kCueBatchSize}},
            {DeferredSubtitlePreload::kCueBatchSize + 1, {DeferredSubtitlePreload::kCueBatchSize, 1}},
        };

        for (const Case& c : cases)
        {
            std::vector<std::size_t> actualSizes;
            for (std::size_t begin = 0; begin < c.cueCount;)
            {
                const std::size_t end = DeferredSubtitlePreload::CueBatchEnd(begin, c.cueCount);
                actualSizes.push_back(end - begin);
                begin = end;
            }
            QCOMPARE(actualSizes, c.expectedSizes);
        }
    }
};

namespace
{
const ::framelift::test::Registrar<DeferredSubtitlePreloadTest> kRegisterDeferredSubtitlePreloadTest{
    "DeferredSubtitlePreloadTest"
};
}

#include "DeferredSubtitlePreloadTests.moc"
