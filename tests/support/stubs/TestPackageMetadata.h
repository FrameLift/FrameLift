#pragma once
#include <framelift/ModuleABI.h>

namespace framelift::generated
{
inline constexpr const char* const kFeatures[] = {"test.feature"};
inline constexpr FrameLiftModuleInfo kPackageModules[] = {
    {"test.package.core",
     "Test Core",
     "Test module",
     {kFeatures, 1},
     {nullptr, 0},
     {nullptr, 0},
     {nullptr, 0},
     {nullptr, 0},
     {nullptr, 0}},
};
inline constexpr FrameLiftPackageInfo kPackageInfo{FRAMELIFT_ABI_VERSION,
                                                 "test.package",
                                                 "framelift.testpackage.core",
                                                 "TestPackage",
                                                 {1, 0, 0},
                                                 "FrameLift",
                                                 "Test package",
                                                 kPackageModules,
                                                 1};
} // namespace framelift::generated

#define FRAMELIFT_MODULE_METADATA ::framelift::generated::kPackageInfo
