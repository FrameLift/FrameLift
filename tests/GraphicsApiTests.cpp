// Unit tests for graphics-backend selection parsing (OpenGL→Vulkan migration, #16).
// GraphicsApi.h is free of SDL/GL so it builds in the standalone native suite.

#include "platform/gfx/GraphicsApi.h"

#include <gtest/gtest.h>

TEST(GraphicsApiTests, DefaultsToOpenGL)
{
    EXPECT_EQ(GraphicsApiFromString("gl"), GraphicsApi::OpenGL);
    EXPECT_EQ(GraphicsApiFromString("opengl"), GraphicsApi::OpenGL);
}

TEST(GraphicsApiTests, ParsesVulkan)
{
    EXPECT_EQ(GraphicsApiFromString("vulkan"), GraphicsApi::Vulkan);
    EXPECT_EQ(GraphicsApiFromString("vk"), GraphicsApi::Vulkan);
}

TEST(GraphicsApiTests, IsCaseInsensitive)
{
    EXPECT_EQ(GraphicsApiFromString("Vulkan"), GraphicsApi::Vulkan);
    EXPECT_EQ(GraphicsApiFromString("VULKAN"), GraphicsApi::Vulkan);
    EXPECT_EQ(GraphicsApiFromString("VK"), GraphicsApi::Vulkan);
}

TEST(GraphicsApiTests, UnknownAndEmptyFallBackToOpenGL)
{
    EXPECT_EQ(GraphicsApiFromString(""), GraphicsApi::OpenGL);
    EXPECT_EQ(GraphicsApiFromString("metal"), GraphicsApi::OpenGL);
    EXPECT_EQ(GraphicsApiFromString("d3d12"), GraphicsApi::OpenGL);
}

TEST(GraphicsApiTests, NameRoundTrips)
{
    EXPECT_EQ(GraphicsApiFromString(GraphicsApiName(GraphicsApi::OpenGL)), GraphicsApi::OpenGL);
    EXPECT_EQ(GraphicsApiFromString(GraphicsApiName(GraphicsApi::Vulkan)), GraphicsApi::Vulkan);
}
