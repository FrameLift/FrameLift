#pragma once
#include <framelift/IModule.h>
#include <framelift/IPlugin.h>
#include <framelift/Log.h>

#include <QtCore/QObject>

#include <exception>
#include <type_traits>

// A runtime plugin DLL/SO is a Qt plugin: its root object is a QObject implementing
// IPlugin, carrying Q_PLUGIN_METADATA whose FILE is the CMake-generated plugin
// metadata JSON (plugin identity + ABI, read by the host via
// QPluginLoader::metaData() without instantiating anything).
//
//   FRAMELIFT_MODULE_ENTRY(MyModule, {
//       .renderOrder = 50,
//   })
//
// A module that has no QML surface opts out explicitly:
//
//   FRAMELIFT_MODULE_ENTRY(MyService, {
//       .qml = false,
//   })

#ifndef FRAMELIFT_PLUGIN_METADATA_JSON
#error "FRAMELIFT_PLUGIN_METADATA_JSON must name the CMake-generated plugin metadata JSON (set by add_framelift_plugin)"
#endif

struct FrameLiftModuleEntryDesc
{
    bool qml = true;
    int renderOrder = 0;
};

#ifndef FRAMELIFT_QML_ENTRY_URL
#define FRAMELIFT_QML_ENTRY_URL nullptr
#endif

namespace framelift::detail
{
template <typename T>
QObject* GetViewModel(IModule* p)
{
    if constexpr (std::is_base_of_v<QObject, T>)
    {
        return static_cast<T*>(p);
    }
    else
    {
        return nullptr;
    }
}

template <typename T>
IModule* CreateModule()
{
    try
    {
        return new T();
    }
    catch (const std::exception& e)
    {
        Log::Error("module constructor threw: {}", e.what());
    }
    catch (...)
    {
        Log::Error("module constructor threw a non-standard exception");
    }
    return nullptr;
}

} // namespace framelift::detail

#define FRAMELIFT_DETAIL_PLUGIN_FACTORY(Type, ...)                                                                     \
    namespace framelift_module_detail                                                                                  \
    {                                                                                                                  \
    static constexpr ::FrameLiftModuleEntryDesc kDesc = __VA_ARGS__;                                                   \
    static_assert(                                                                                                     \
        !kDesc.qml || std::is_base_of_v<QObject, Type>,                                                                \
        "FRAMELIFT_MODULE_ENTRY: " #Type " is not a QObject; add .qml = false or inherit QObject first"                \
    );                                                                                                                 \
    }                                                                                                                  \
    class FrameLiftPluginFactory final : public QObject, public IPlugin                                                \
    {                                                                                                                  \
        Q_OBJECT                                                                                                       \
        Q_PLUGIN_METADATA(IID FrameLiftPlugin_IID FILE FRAMELIFT_PLUGIN_METADATA_JSON)                                 \
        Q_INTERFACES(IPlugin)                                                                                          \
                                                                                                                       \
    public:                                                                                                            \
        void SetLogSink(Log::SinkFn fn) noexcept override                                                              \
        {                                                                                                              \
            Log::SetSink(fn);                                                                                          \
        }                                                                                                              \
        IModule* CreateModule() noexcept override                                                                      \
        {                                                                                                              \
            return ::framelift::detail::CreateModule<Type>();                                                          \
        }                                                                                                              \
        void DestroyModule(IModule* module) noexcept override                                                          \
        {                                                                                                              \
            delete module;                                                                                             \
        }                                                                                                              \
        QObject* GetViewModel(IModule* module) noexcept override                                                       \
        {                                                                                                              \
            return framelift_module_detail::kDesc.qml ? ::framelift::detail::GetViewModel<Type>(module) : nullptr;     \
        }                                                                                                              \
        const char* QmlEntryUrl() noexcept override                                                                    \
        {                                                                                                              \
            return framelift_module_detail::kDesc.qml ? FRAMELIFT_QML_ENTRY_URL : nullptr;                             \
        }                                                                                                              \
        int RenderOrder() noexcept override                                                                            \
        {                                                                                                              \
            return framelift_module_detail::kDesc.qml ? framelift_module_detail::kDesc.renderOrder : 0;                \
        }                                                                                                              \
    };

#ifdef FRAMELIFT_SUPPRESS_MODULE_ENTRY
#define FRAMELIFT_MODULE_ENTRY(Type, ...)
#else
#define FRAMELIFT_MODULE_ENTRY(Type, ...) FRAMELIFT_DETAIL_PLUGIN_FACTORY(Type, __VA_ARGS__)
#endif
