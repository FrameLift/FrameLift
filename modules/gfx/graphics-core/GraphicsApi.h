#pragma once

#include <string_view>

#ifndef FRAMELIFT_MODULE_GRAPHICS_VULKAN
#define FRAMELIFT_MODULE_GRAPHICS_VULKAN 1
#endif

// The graphics presentation backend the host renders through. Selected at startup
// from the [graphics] backend setting. OpenGL is the only backend implemented today;
// Vulkan is planned (see the OpenGL→Vulkan migration, issues #15–#18).
enum class GraphicsApi
{
    OpenGL,
    Vulkan,
};

// Parse a backend name (case-insensitive) from the settings file. Unknown / empty
// values fall back to OpenGL. Pure (no SDL/GL) so it is unit-testable.
inline GraphicsApi GraphicsApiFromString(std::string_view name)
{
    // Tiny case-insensitive compare against the known names; the set is fixed and small.
    auto iequals = [](std::string_view a, std::string_view b)
    {
        if (a.size() != b.size())
        {
            return false;
        }
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            char ca = a[i], cb = b[i];
            if (ca >= 'A' && ca <= 'Z')
            {
                ca = static_cast<char>(ca - 'A' + 'a');
            }
            if (cb >= 'A' && cb <= 'Z')
            {
                cb = static_cast<char>(cb - 'A' + 'a');
            }
            if (ca != cb)
            {
                return false;
            }
        }
        return true;
    };

#if FRAMELIFT_MODULE_GRAPHICS_VULKAN
    if (iequals(name, "vulkan") || iequals(name, "vk"))
    {
        return GraphicsApi::Vulkan;
    }
#else
    (void)name;
#endif
    return GraphicsApi::OpenGL;
}

// Canonical lowercase name for a backend (for logging / round-tripping).
inline const char* GraphicsApiName(GraphicsApi api)
{
    switch (api)
    {
    case GraphicsApi::Vulkan:
#if FRAMELIFT_MODULE_GRAPHICS_VULKAN
        return "vulkan";
#else
        break;
#endif
    case GraphicsApi::OpenGL:
        break;
    }
    return "gl";
}
