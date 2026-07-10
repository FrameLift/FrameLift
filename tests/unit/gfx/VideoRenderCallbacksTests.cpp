#include "VideoRenderCallbacks.h"

#include "QtTestRunner.h"

#include <QtTest/QtTest>
#include <memory>

class VideoRenderCallbacksTests final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void ClearingBindingStopsExistingNodeCallbackProxy()
    {
        auto callbacks = std::make_shared<VideoRenderCallbacks>();
        const std::shared_ptr<VideoRenderCallbacks> existingNodeBinding = callbacks;
        int prepareCalls = 0;
        int renderCalls = 0;

        callbacks->Set(
            [&prepareCalls](int, int, int, int)
            {
                ++prepareCalls;
            },
            [&renderCalls](int, int, int, int)
            {
                ++renderCalls;
            }
        );

        existingNodeBinding->Prepare(0, 0, 1280, 720);
        existingNodeBinding->Render(0, 0, 1280, 720);
        QCOMPARE(prepareCalls, 1);
        QCOMPARE(renderCalls, 1);

        callbacks->Set({}, {});
        existingNodeBinding->Prepare(0, 0, 1280, 720);
        existingNodeBinding->Render(0, 0, 1280, 720);
        QCOMPARE(prepareCalls, 1);
        QCOMPARE(renderCalls, 1);
    }
};

namespace
{
const ::framelift::test::Registrar<VideoRenderCallbacksTests> kRegisterVideoRenderCallbacksTests{
    "VideoRenderCallbacksTests"
};
}

#include "VideoRenderCallbacksTests.moc"
