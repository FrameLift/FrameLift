#pragma once

#include <vulkan/vulkan.h>

#include <framelift/Log.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>

// Shared error-handling and debug helpers for the Vulkan module.
//
// Error policy (see also VulkanGraphicsBackend.h):
//   VK_CHECK_THROW      — backend construction; failures abort backend creation, which
//                         IsSupported() converts into a clean "unsupported" probe result.
//   VK_CHECK_LOG_RETURN — renderer init / build paths that report failure via bool.
//   VK_CHECK_LOG        — per-frame hot paths; logs once per call site, never throws.

#define VK_CHECK_THROW(expr, msg)                                                                                      \
    do                                                                                                                 \
    {                                                                                                                  \
        const VkResult vkCheckResult_ = (expr);                                                                        \
        if (vkCheckResult_ != VK_SUCCESS)                                                                              \
        {                                                                                                              \
            throw std::runtime_error(                                                                                  \
                std::string(msg) + " (VkResult " + std::to_string(static_cast<int>(vkCheckResult_)) + ")"              \
            );                                                                                                         \
        }                                                                                                              \
    } while (false)

#define VK_CHECK_LOG_RETURN(expr, msg, ret)                                                                            \
    do                                                                                                                 \
    {                                                                                                                  \
        const VkResult vkCheckResult_ = (expr);                                                                        \
        if (vkCheckResult_ != VK_SUCCESS)                                                                              \
        {                                                                                                              \
            Log::Error("{} (VkResult {})", msg, static_cast<int>(vkCheckResult_));                                     \
            return ret;                                                                                                \
        }                                                                                                              \
    } while (false)

#define VK_CHECK_LOG(expr, msg)                                                                                        \
    do                                                                                                                 \
    {                                                                                                                  \
        const VkResult vkCheckResult_ = (expr);                                                                        \
        if (vkCheckResult_ != VK_SUCCESS)                                                                              \
        {                                                                                                              \
            static bool vkCheckLogged_ = false;                                                                        \
            if (!vkCheckLogged_)                                                                                       \
            {                                                                                                          \
                vkCheckLogged_ = true;                                                                                 \
                Log::Error(                                                                                            \
                    "{} (VkResult {}; further occurrences suppressed)", msg, static_cast<int>(vkCheckResult_)          \
                );                                                                                                     \
            }                                                                                                          \
        }                                                                                                              \
    } while (false)

namespace VulkanUtil
{
// Installed by VulkanGraphicsBackend when VK_EXT_debug_utils is available; stays null
// otherwise (release builds, validation off), making SetObjectName a no-op.
inline PFN_vkSetDebugUtilsObjectNameEXT g_setObjectNameFn = nullptr;

// Attach a debug name to a Vulkan handle so validation messages and captures identify
// our objects. Accepts both dispatchable (pointer) and non-dispatchable handles.
template <typename HandleT>
inline void SetObjectName(VkDevice device, VkObjectType type, HandleT handle, const char* name)
{
    if (g_setObjectNameFn == nullptr || device == VK_NULL_HANDLE)
    {
        return;
    }
    uint64_t raw = 0;
    if constexpr (std::is_pointer_v<HandleT>)
    {
        raw = reinterpret_cast<uint64_t>(handle);
    }
    else
    {
        raw = static_cast<uint64_t>(handle);
    }
    if (raw == 0)
    {
        return;
    }
    VkDebugUtilsObjectNameInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
    info.objectType = type;
    info.objectHandle = raw;
    info.pObjectName = name;
    g_setObjectNameFn(device, &info);
}
} // namespace VulkanUtil
