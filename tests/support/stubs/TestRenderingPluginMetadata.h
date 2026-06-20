#pragma once
#include <framelift/PluginABI.h>

namespace framelift::generated
{
inline constexpr const char* const kFeatures[] = {"test.rendering"};
inline constexpr const char* const kOptionalFeatures[] = {"test.optional"};
inline constexpr FrameLiftModuleInfo kPluginModules[] = {
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
inline constexpr FrameLiftPluginInfo kPluginInfo{FRAMELIFT_PLUGIN_ABI_MAJOR,
                                                 FRAMELIFT_PLUGIN_ABI_MINOR,
                                                 FRAMELIFT_PLUGIN_ABI_PATCH,
                                                 "test.rendering",
                                                 "Acme.RenderingDummy.Core",
                                                 "RenderingDummy",
                                                 {2, 5, 9},
                                                 "Acme",
                                                 "Does a thing",
                                                 kPluginModules,
                                                 1};
} // namespace framelift::generated

#define FRAMELIFT_PLUGIN_METADATA ::framelift::generated::kPluginInfo
