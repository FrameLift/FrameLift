#include <framelift/ModuleEntry.h>

#include <gtest/gtest.h>

// Exercises FRAMELIFT_MODULE_ENTRY with .render = false on a module entry that draws
// nothing. Lives in its own binary because the macro's extern "C" symbols
// would clash with ModuleEntryTests.cpp's. The inverse misuse — a
// non-renderable type with render left true — is a static_assert and cannot
// be runtime-tested.

namespace
{
class NonRenderingDummy final : public IModule
{
};
} // namespace

FRAMELIFT_MODULE_ENTRY(NonRenderingDummy, {
    .render = false,
})

TEST(ModuleEntryNoRenderTest, OptionalMetadataDefaultsToNull)
{
    const FrameLiftPackageInfo* info = framelift_module_info();
    ASSERT_NE(info, nullptr);
    EXPECT_STREQ(info->packageId, "test.norender");
    EXPECT_STREQ(info->name, "NonRenderingDummy");
    EXPECT_EQ(info->publisher, nullptr);
    EXPECT_EQ(info->description, nullptr);
}

TEST(ModuleEntryNoRenderTest, RenderableIsNullAndOrderZero)
{
    IModule* module = framelift_create("test.norender.core");
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(framelift_get_renderable("test.norender.core", module), nullptr);
    EXPECT_EQ(framelift_render_order("test.norender.core"), 0);
    framelift_destroy(module);
}

TEST(ModuleEntryNoRenderTest, GetRenderableHelperHandlesBothKinds)
{
    NonRenderingDummy dummy;
    EXPECT_EQ(framelift::detail::GetRenderable<NonRenderingDummy>(&dummy), nullptr);
}

TEST(ModuleEntryNoRenderTest, PlainModuleHasNoOptionalInterfaces)
{
    NonRenderingDummy dummy;
    EXPECT_EQ(dummy.QueryInterface(IHotkeyProvider::InterfaceId), nullptr);
    EXPECT_EQ(dummy.QueryInterface(IEventHandler::InterfaceId), nullptr);
    EXPECT_EQ(dummy.QueryInterface(IMediaEventHandler::InterfaceId), nullptr);
    EXPECT_EQ(dummy.QueryInterface(IShutdownHandler::InterfaceId), nullptr);
}
