#pragma once
#include <framelift/ModuleABI.h>

namespace framelift::generated
{
inline constexpr FrameLiftModuleInfo kPackageModules[] = {
    {"test.multi.core",
     "Multi Core",
     "First module",
     {nullptr, 0},
     {nullptr, 0},
     {nullptr, 0},
     {nullptr, 0},
     {nullptr, 0},
     {nullptr, 0}},
    {"test.multi.extra",
     "Multi Extra",
     "Second module (draws nothing)",
     {nullptr, 0},
     {nullptr, 0},
     {nullptr, 0},
     {nullptr, 0},
     {nullptr, 0},
     {nullptr, 0}},
};
inline constexpr FrameLiftPackageInfo kPackageInfo{FRAMELIFT_ABI_VERSION,
                                                 "test.multi",
                                                 "acme.multi",
                                                 "Multi",
                                                 {1, 0, 0},
                                                 "Acme",
                                                 "Multi-module test package",
                                                 kPackageModules,
                                                 2};
} // namespace framelift::generated

#define FRAMELIFT_MODULE_METADATA ::framelift::generated::kPackageInfo
