#pragma once
#include <framelift/ModuleABI.h>

namespace framelift::generated
{
inline constexpr const char* const kFeatures[] = {"test.rendering"};
inline constexpr const char* const kOptionalFeatures[] = {"test.optional"};
inline constexpr FrameLiftModuleInfo kPackageModules[] = {
    {"test.rendering.core",
     "Rendering Core",
     "Rendering dummy module",
     {kFeatures, 1},
     {nullptr, 0},
     {nullptr, 0},
     {nullptr, 0},
     {kOptionalFeatures, 1},
     {nullptr, 0}},
};
inline constexpr FrameLiftPackageInfo kPackageInfo{FRAMELIFT_ABI_VERSION,
                                                 "test.rendering",
                                                 "Acme.RenderingDummy.Core",
                                                 "RenderingDummy",
                                                 {2, 5, 9},
                                                 "Acme",
                                                 "Does a thing",
                                                 kPackageModules,
                                                 1};
} // namespace framelift::generated

#define FRAMELIFT_MODULE_METADATA ::framelift::generated::kPackageInfo
