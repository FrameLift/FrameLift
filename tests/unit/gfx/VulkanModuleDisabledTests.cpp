#include "GraphicsApi.h"
#include "VideoDecodeMode.h"

#include "QtTestRunner.h"

#include <QtTest/QtTest>

#include <algorithm>

class VulkanModuleDisabledTests final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void BackendSelectionNormalizesToOpenGl()
    {
        // With Vulkan compiled out, FL_BACKEND values that request Vulkan (or auto) collapse to
        // OpenGL, and the canonical name is always "gl".
        QVERIFY((GraphicsApiFromString("")) == (GraphicsApi::OpenGL));
        QVERIFY((GraphicsApiFromString("auto")) == (GraphicsApi::OpenGL));
        QVERIFY((GraphicsApiFromString("vulkan")) == (GraphicsApi::OpenGL));
        QVERIFY((GraphicsApiFromString("vk")) == (GraphicsApi::OpenGL));
        QVERIFY(::framelift::test::CStringEqual(GraphicsApiName(GraphicsApi::Vulkan), "gl"));
    }

    void VulkanDecodeModesNormalizeToAuto()
    {
        QVERIFY((VideoDecodeModeFromString("vulkan-zero-copy")) == (VideoDecodeMode::Auto));
        QVERIFY((VideoDecodeModeFromString("vulkan")) == (VideoDecodeMode::Auto));
        QVERIFY(::framelift::test::CStringEqual(VideoDecodeModeName(VideoDecodeMode::VulkanZeroCopy), "auto"));
        QVERIFY(::framelift::test::CStringEqual(VideoDecodeModeName(VideoDecodeMode::Vulkan), "auto"));
        QVERIFY((HwBackendFromVideoDecodeMode(VideoDecodeMode::Vulkan)) == (HwBackend::None));
    }

    void AutoPreferenceOmitsVulkanModes()
    {
        const auto preference = AutoVideoDecodePreference();
        QVERIFY(
            (std::find(preference.begin(), preference.end(), VideoDecodeMode::VulkanZeroCopy)) == (preference.end())
        );
        QVERIFY((std::find(preference.begin(), preference.end(), VideoDecodeMode::Vulkan)) == (preference.end()));
    }
};

namespace
{
const ::framelift::test::Registrar<VulkanModuleDisabledTests> kRegisterVulkanModuleDisabledTests{
    "VulkanModuleDisabledTests"
};
}

#include "VulkanModuleDisabledTests.moc"
