#pragma once

#include "GraphicsApi.h"
#include "SettingsRegistry.h"

#include <string>

// Graphics settings — owned by the gfx/graphics-core module.

#ifndef FRAMELIFT_MODULE_GRAPHICS_VULKAN
#define FRAMELIFT_MODULE_GRAPHICS_VULKAN 1
#endif

#if FRAMELIFT_MODULE_GRAPHICS_VULKAN
#define FRAMELIFT_DEFAULT_GRAPHICS_BACKEND "vulkan"
#define FRAMELIFT_SETTINGS_GRAPHICS_BACKEND_DESC \
    "Video rendering backend: vulkan or gl (falls back to gl if Vulkan is unavailable). Takes effect on restart."
#else
#define FRAMELIFT_DEFAULT_GRAPHICS_BACKEND "gl"
#define FRAMELIFT_SETTINGS_GRAPHICS_BACKEND_DESC "Video rendering backend: gl. Takes effect on restart."
#endif

struct GraphicsSettings
{
    std::string backend = FRAMELIFT_DEFAULT_GRAPHICS_BACKEND;
};

inline void RegisterGraphicsSettings(SettingsRegistry& reg, GraphicsSettings& s)
{
    reg.AddString("graphics.backend", s.backend, FRAMELIFT_SETTINGS_GRAPHICS_BACKEND_DESC,
                  [&s] { return std::string(GraphicsApiName(GraphicsApiFromString(s.backend))); });

    // The on-disk backend string is always normalized to a canonical api name.
    reg.AddPostLoad([&s](const std::set<std::string>&)
                    { s.backend = GraphicsApiName(GraphicsApiFromString(s.backend)); });
}
