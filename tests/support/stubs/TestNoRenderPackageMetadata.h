#pragma once
#include <framelift/ModuleABI.h>

namespace framelift::generated
{
inline constexpr FrameLiftModuleInfo kPackageModules[] = {
    {"test.norender.core",
     "No Render Core",
     nullptr,
     {nullptr, 0},
     {nullptr, 0},
     {nullptr, 0},
     {nullptr, 0},
     {nullptr, 0},
     {nullptr, 0}},
};
inline constexpr FrameLiftPackageInfo kPackageInfo{FRAMELIFT_ABI_VERSION,
                                                 "test.norender",
                                                 "NonRenderingDummy.Core",
                                                 "NonRenderingDummy",
                                                 {1, 0, 0},
                                                 nullptr,
                                                 nullptr,
                                                 kPackageModules,
                                                 1};
} // namespace framelift::generated

#define FRAMELIFT_MODULE_METADATA ::framelift::generated::kPackageInfo
