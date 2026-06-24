#include "GlGraphicsBackend.h"
#include "IGraphicsBackend.h"
#if FRAMELIFT_MODULE_GRAPHICS_VULKAN
#include "VulkanGraphicsBackend.h"
#endif

#include <framelift/Log.h>

// Selects the concrete graphics backend. Kept separate from the GL and Vulkan backend
// TUs so neither references the other — only this factory knows about both.
std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(GraphicsApi api)
{
#if FRAMELIFT_MODULE_GRAPHICS_VULKAN
    if (api == GraphicsApi::Vulkan)
    {
        // Probe before committing the window's SDL_WINDOW_VULKAN flag: if no usable
        // Vulkan device is present, fall back to OpenGL instead of failing at startup
        // (matters now that Vulkan is the default).
        if (VulkanGraphicsBackend::IsSupported())
        {
            return std::make_unique<VulkanGraphicsBackend>();
        }
        Log::Warn("graphics.backend=vulkan requested but no usable Vulkan device was found; using OpenGL.");
    }
#else
    (void)api;
#endif
    return std::make_unique<GlGraphicsBackend>();
}
