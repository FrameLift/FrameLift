#pragma once
#include <framelift/IModule.h>
#include <framelift/IRenderable.h>
#include <framelift/Log.h>
#include <framelift/ModuleABI.h>

#include <exception>
#include <type_traits>

#ifndef FRAMELIFT_MODULE_METADATA_HEADER
#error "FRAMELIFT_MODULE_METADATA_HEADER must name the CMake-generated package metadata header"
#endif

#include FRAMELIFT_MODULE_METADATA_HEADER

#ifndef FRAMELIFT_MODULE_METADATA
#error "Generated package metadata header must define FRAMELIFT_MODULE_METADATA"
#endif

// Place FRAMELIFT_MODULE_ENTRY at file scope in the module header, after the
// module entry class declaration.
// Package/module identity comes from the JSON-authored metadata header generated
// by add_framelift_plugin(... PLUGIN_JSON <file>). The C++ descriptor only
// carries runtime traits that depend on the module entry type.
//
//   FRAMELIFT_MODULE_ENTRY(MyPanel, {
//       .renderOrder = 50,
//   })
//
// A module that draws nothing opts out of rendering explicitly:
//
//   FRAMELIFT_MODULE_ENTRY(MyService, {
//       .render = false,
//   })

struct FrameLiftModuleEntryDesc
{
    bool render = true;
    int renderOrder = 0;
};

#ifdef _WIN32
#define FRAMELIFT_MODULE_API __declspec(dllexport)
#else
#define FRAMELIFT_MODULE_API __attribute__((visibility("default")))
#endif

namespace framelift::detail
{
template <typename T>
IRenderable* GetRenderable(IModule* p)
{
    if constexpr (std::is_base_of_v<IRenderable, T>)
    {
        return static_cast<T*>(p);
    }
    else
    {
        return nullptr;
    }
}
} // namespace framelift::detail

#ifdef FRAMELIFT_SUPPRESS_MODULE_ENTRY
#define FRAMELIFT_MODULE_ENTRY(Type, ...)
#else
#define FRAMELIFT_MODULE_ENTRY(Type, ...)                                                                                   \
    namespace framelift_module_detail                                                                                       \
    {                                                                                                                       \
    inline constexpr ::FrameLiftModuleEntryDesc kDesc = __VA_ARGS__;                                                        \
    static_assert(                                                                                                          \
        !kDesc.render || std::is_base_of_v<IRenderable, Type>,                                                              \
        "FRAMELIFT_MODULE_ENTRY: " #Type " does not implement IRenderable; add .render = false or derive IRenderable"      \
    );                                                                                                                      \
    }                                                                                                                       \
    extern "C"                                                                                                              \
    {                                                                                                                       \
        FRAMELIFT_MODULE_API const FrameLiftPackageInfo* framelift_module_info()                                              \
        {                                                                                                                   \
            return &FRAMELIFT_MODULE_METADATA;                                                                              \
        }                                                                                                                   \
        FRAMELIFT_MODULE_API void framelift_set_log_sink(Log::SinkFn fn)                                                     \
        {                                                                                                                   \
            Log::SetSink(fn);                                                                                               \
        }                                                                                                                   \
        FRAMELIFT_MODULE_API IModule* framelift_create()                                                                     \
        {                                                                                                                   \
            try                                                                                                             \
            {                                                                                                               \
                return new Type();                                                                                          \
            }                                                                                                               \
            catch (const std::exception& e)                                                                                 \
            {                                                                                                               \
                Log::Error("{}: constructor threw: {}", FRAMELIFT_MODULE_METADATA.name, e.what());                           \
            }                                                                                                               \
            catch (...)                                                                                                     \
            {                                                                                                               \
                Log::Error("{}: constructor threw a non-standard exception", FRAMELIFT_MODULE_METADATA.name);                \
            }                                                                                                               \
            return nullptr;                                                                                                 \
        }                                                                                                                   \
        FRAMELIFT_MODULE_API void framelift_destroy(IModule* f)                                                              \
        {                                                                                                                   \
            delete f;                                                                                                       \
        }                                                                                                                   \
        FRAMELIFT_MODULE_API IRenderable* framelift_get_renderable(IModule* f)                                               \
        {                                                                                                                   \
            return framelift_module_detail::kDesc.render ? ::framelift::detail::GetRenderable<Type>(f) : nullptr;            \
        }                                                                                                                   \
        FRAMELIFT_MODULE_API int framelift_render_order()                                                                    \
        {                                                                                                                   \
            return framelift_module_detail::kDesc.render ? framelift_module_detail::kDesc.renderOrder : 0;                  \
        }                                                                                                                   \
    }
#endif
