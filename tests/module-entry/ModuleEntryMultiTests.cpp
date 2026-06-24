#include <framelift/IModule.h>
#include <framelift/IRenderable.h>
#include <framelift/ModuleBase.h>
#include <framelift/ModuleEntry.h>

#include <gtest/gtest.h>

// Exercises the multi-module package macros (FRAMELIFT_PACKAGE_BEGIN / FRAMELIFT_MODULE
// / FRAMELIFT_PACKAGE_END): one DLL carrying two modules, dispatched by module id.
// Lives in its own binary because the extern "C" symbols would clash with the other
// module-entry suites.

namespace
{
class MultiCore final : public ModuleBase, public IRenderable
{
public:
    void Render(UIContext&) noexcept override
    {
    }

protected:
    const char* ModuleName() const override
    {
        return "MultiCore";
    }
};

class MultiExtra final : public IModule
{
};
} // namespace

FRAMELIFT_PACKAGE_BEGIN()
FRAMELIFT_MODULE("test.multi.core", MultiCore, {.renderOrder = 11})
FRAMELIFT_MODULE("test.multi.extra", MultiExtra, {.render = false})
FRAMELIFT_PACKAGE_END()

TEST(ModuleEntryMultiTest, InfoCarriesBothModules)
{
    const FrameLiftPackageInfo* info = framelift_module_info();
    ASSERT_NE(info, nullptr);
    EXPECT_STREQ(info->packageId, "test.multi");
    EXPECT_STREQ(info->moduleFile, "acme.multi");
    ASSERT_EQ(info->moduleCount, 2);
    EXPECT_STREQ(info->modules[0].id, "test.multi.core");
    EXPECT_STREQ(info->modules[1].id, "test.multi.extra");
}

TEST(ModuleEntryMultiTest, DispatchesCreateByModuleId)
{
    IModule* core = framelift_create("test.multi.core");
    ASSERT_NE(core, nullptr);
    EXPECT_NE(framelift_get_renderable("test.multi.core", core), nullptr);
    EXPECT_EQ(framelift_render_order("test.multi.core"), 11);
    framelift_destroy(core);

    IModule* extra = framelift_create("test.multi.extra");
    ASSERT_NE(extra, nullptr);
    EXPECT_EQ(framelift_get_renderable("test.multi.extra", extra), nullptr); // .render = false
    EXPECT_EQ(framelift_render_order("test.multi.extra"), 0);
    framelift_destroy(extra);
}

TEST(ModuleEntryMultiTest, UnknownModuleIdReturnsNull)
{
    // A multi-module package serves nothing for an unknown id (no single-module fallback).
    EXPECT_EQ(framelift_create("test.multi.nope"), nullptr);
    EXPECT_EQ(framelift_render_order("test.multi.nope"), 0);
}
