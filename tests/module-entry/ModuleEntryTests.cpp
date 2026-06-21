#include <framelift/ModuleBase.h>
#include <framelift/ModuleEntry.h>

#include <gtest/gtest.h>

// Exercises FRAMELIFT_MODULE_ENTRY on a rendering module entry: the macro emits the
// extern "C" entry points into this TU, so the tests call them directly. Only
// one macro invocation fits per binary (the C symbols would clash) — the
// .render = false path lives in ModuleEntryNoRenderTests.cpp.

namespace
{
class RenderingDummy final : public ModuleBase, public IRenderable
{
public:
    void Render(UIContext&) noexcept override
    {
    }

protected:
    const char* ModuleName() const override
    {
        return "RenderingDummy";
    }
};
} // namespace

FRAMELIFT_MODULE_ENTRY(RenderingDummy, {
    .renderOrder = 7,
})

TEST(ModuleEntryTest, InfoCarriesAbiAndIdentity)
{
    const FrameLiftPackageInfo* info = framelift_module_info();
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->abiVersion, FRAMELIFT_ABI_VERSION);
    EXPECT_STREQ(info->packageId, "test.rendering");
    EXPECT_STREQ(info->moduleFile, "acme.renderingdummy.core");
    EXPECT_STREQ(info->name, "RenderingDummy");
    EXPECT_EQ(info->version[0], 2);
    EXPECT_EQ(info->version[1], 5);
    EXPECT_EQ(info->version[2], 9);
    EXPECT_STREQ(info->publisher, "Acme");
    EXPECT_STREQ(info->description, "Does a thing");
    ASSERT_EQ(info->moduleCount, 1);
    EXPECT_STREQ(info->modules[0].id, "test.rendering.core");
    ASSERT_EQ(info->modules[0].optionalFeatures.count, 1);
    EXPECT_STREQ(info->modules[0].optionalFeatures.items[0], "test.optional");
}

TEST(ModuleEntryTest, RenderOrderComesFromDescriptor)
{
    EXPECT_EQ(framelift_render_order("test.rendering.core"), 7);
}

TEST(ModuleEntryTest, RenderableRoundTrip)
{
    IModule* module = framelift_create("test.rendering.core");
    ASSERT_NE(module, nullptr);
    EXPECT_NE(framelift_get_renderable("test.rendering.core", module), nullptr);
    framelift_destroy(module);
}

TEST(ModuleEntryTest, SingleModuleServesAnyRequestedId)
{
    // A single-module package serves its lone module regardless of the requested id.
    IModule* module = framelift_create("anything");
    ASSERT_NE(module, nullptr);
    framelift_destroy(module);
}

TEST(ModuleEntryTest, ModuleBaseExposesConvenienceInterfaces)
{
    RenderingDummy module;
    EXPECT_NE(module.QueryInterface(IHotkeyProvider::InterfaceId), nullptr);
    EXPECT_NE(module.QueryInterface(IEventHandler::InterfaceId), nullptr);
    EXPECT_NE(module.QueryInterface(IMediaEventHandler::InterfaceId), nullptr);
    EXPECT_NE(module.QueryInterface(IShutdownHandler::InterfaceId), nullptr);
    EXPECT_EQ(module.QueryInterface("framelift.missing"), nullptr);
}

TEST(ModuleEntryTest, DescriptorDefaults)
{
    constexpr FrameLiftModuleEntryDesc desc{};
    EXPECT_TRUE(desc.render);
    EXPECT_EQ(desc.renderOrder, 0);
}
