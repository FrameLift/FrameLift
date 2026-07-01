#include "GlGraphicsBackend.h"
#include "IGraphicsBackend.h"
#if FRAMELIFT_MODULE_GRAPHICS_VULKAN
#include "VulkanGraphicsBackend.h"
#endif

#include <exception>
#include <framelift/Log.h>
#include <stdexcept>

// Selects the concrete graphics backend. Kept separate from the GL and Vulkan backend
// TUs so neither references the other — only this factory knows about both.
std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(GraphicsApi api)
{
#if FRAMELIFT_MODULE_GRAPHICS_VULKAN
    if (api == GraphicsApi::Vulkan || api == GraphicsApi::Auto)
    {
        try
        {
            return std::make_unique<VulkanGraphicsBackend>();
        }
        catch (const std::exception& e)
        {
            if (api == GraphicsApi::Vulkan)
            {
                throw;
            }
            Log::Warn("Vulkan initialization failed in auto mode ({}); using OpenGL.", e.what());
        }
    }
#else
    (void)api;
#endif
    return std::make_unique<GlGraphicsBackend>();
}
