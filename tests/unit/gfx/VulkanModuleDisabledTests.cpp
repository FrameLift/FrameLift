#include "VideoDecodeMode.h"
#include "GraphicsApi.h"

#include <gtest/gtest.h>

#include <algorithm>

TEST(VulkanModuleDisabledTests, BackendSelectionNormalizesToOpenGl)
{
    // With Vulkan compiled out, FL_BACKEND values that request Vulkan (or auto) collapse to
    // OpenGL, and the canonical name is always "gl".
    EXPECT_EQ(GraphicsApiFromString(""), GraphicsApi::OpenGL);
    EXPECT_EQ(GraphicsApiFromString("auto"), GraphicsApi::OpenGL);
    EXPECT_EQ(GraphicsApiFromString("vulkan"), GraphicsApi::OpenGL);
    EXPECT_EQ(GraphicsApiFromString("vk"), GraphicsApi::OpenGL);
    EXPECT_STREQ(GraphicsApiName(GraphicsApi::Vulkan), "gl");
}

TEST(VulkanModuleDisabledTests, VulkanDecodeModesNormalizeToAuto)
{
    EXPECT_EQ(VideoDecodeModeFromString("vulkan-zero-copy"), VideoDecodeMode::Auto);
    EXPECT_EQ(VideoDecodeModeFromString("vulkan"), VideoDecodeMode::Auto);
    EXPECT_STREQ(VideoDecodeModeName(VideoDecodeMode::VulkanZeroCopy), "auto");
    EXPECT_STREQ(VideoDecodeModeName(VideoDecodeMode::Vulkan), "auto");
    EXPECT_EQ(HwBackendFromVideoDecodeMode(VideoDecodeMode::Vulkan), HwBackend::None);
}

TEST(VulkanModuleDisabledTests, AutoPreferenceOmitsVulkanModes)
{
    const auto preference = AutoVideoDecodePreference();
    EXPECT_EQ(std::find(preference.begin(), preference.end(), VideoDecodeMode::VulkanZeroCopy), preference.end());
    EXPECT_EQ(std::find(preference.begin(), preference.end(), VideoDecodeMode::Vulkan), preference.end());
}
