#pragma once
#include <framelift/PluginABI.h>

namespace framelift::generated
{
inline constexpr const char* const kFeatures[] = {"test.feature"};
inline constexpr FrameLiftModuleInfo kPluginModules[] = {
    {"test.plugin.core",
     "Test Core",
     "Test module",
     {kFeatures, 1},
     {nullptr, 0},
     {nullptr, 0},
     {nullptr, 0},
     {nullptr, 0},
     {nullptr, 0}},
};
inline constexpr FrameLiftPluginInfo kPluginInfo{FRAMELIFT_PLUGIN_ABI_MAJOR,
                                                 FRAMELIFT_PLUGIN_ABI_MINOR,
                                                 FRAMELIFT_PLUGIN_ABI_PATCH,
                                                 "test.plugin",
                                                 "FrameLift.TestPlugin.Core",
                                                 "TestPlugin",
                                                 {1, 0, 0},
                                                 "FrameLift",
                                                 "Test plugin",
                                                 kPluginModules,
                                                 1};
} // namespace framelift::generated

#define FRAMELIFT_PLUGIN_METADATA ::framelift::generated::kPluginInfo
