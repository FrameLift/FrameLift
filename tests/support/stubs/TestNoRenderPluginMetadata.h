#pragma once
#include <framelift/PluginABI.h>

namespace framelift::generated
{
inline constexpr FrameLiftModuleInfo kPluginModules[] = {
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
inline constexpr FrameLiftPluginInfo kPluginInfo{FRAMELIFT_PLUGIN_ABI_MAJOR,
                                                 FRAMELIFT_PLUGIN_ABI_MINOR,
                                                 FRAMELIFT_PLUGIN_ABI_PATCH,
                                                 "test.norender",
                                                 "NonRenderingDummy.Core",
                                                 "NonRenderingDummy",
                                                 {1, 0, 0},
                                                 nullptr,
                                                 nullptr,
                                                 kPluginModules,
                                                 1};
} // namespace framelift::generated

#define FRAMELIFT_PLUGIN_METADATA ::framelift::generated::kPluginInfo
