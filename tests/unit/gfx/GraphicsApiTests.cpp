// Unit tests for graphics-backend selection parsing.
// GraphicsApi.h is free of window-system and GL types so it builds in the standalone native suite.

#include "GraphicsApi.h"

#include "QtTestRunner.h"

#include <QtTest/QtTest>

class GraphicsApiTests final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void DefaultsToOpenGL()
    {
        QVERIFY((GraphicsApiFromString("gl")) == (GraphicsApi::OpenGL));
        QVERIFY((GraphicsApiFromString("opengl")) == (GraphicsApi::OpenGL));
    }

    void ParsesVulkan()
    {
        QVERIFY((GraphicsApiFromString("vulkan")) == (GraphicsApi::Vulkan));
        QVERIFY((GraphicsApiFromString("vk")) == (GraphicsApi::Vulkan));
    }

    void IsCaseInsensitive()
    {
        QVERIFY((GraphicsApiFromString("Vulkan")) == (GraphicsApi::Vulkan));
        QVERIFY((GraphicsApiFromString("VULKAN")) == (GraphicsApi::Vulkan));
        QVERIFY((GraphicsApiFromString("VK")) == (GraphicsApi::Vulkan));
    }

    void UnknownFallsBackToOpenGLAndEmptyUsesAuto()
    {
        QVERIFY((GraphicsApiFromString("")) == (GraphicsApi::Auto));
        QVERIFY((GraphicsApiFromString("metal")) == (GraphicsApi::OpenGL));
        QVERIFY((GraphicsApiFromString("d3d12")) == (GraphicsApi::OpenGL));
    }

    void NameRoundTrips()
    {
        QVERIFY((GraphicsApiFromString(GraphicsApiName(GraphicsApi::OpenGL))) == (GraphicsApi::OpenGL));
        QVERIFY((GraphicsApiFromString(GraphicsApiName(GraphicsApi::Vulkan))) == (GraphicsApi::Vulkan));
    }
};

namespace
{
const ::framelift::test::Registrar<GraphicsApiTests> kRegisterGraphicsApiTests{"GraphicsApiTests"};
}

#include "GraphicsApiTests.moc"
