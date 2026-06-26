#pragma once
#include <framelift/IModule.h>
#include <framelift/IPackage.h>
#include <framelift/Log.h>

#include <QtCore/QObject>

#include <cstring>
#include <exception>
#include <type_traits>

// A package DLL is a Qt plugin: its root object is a QObject implementing IPackage,
// carrying Q_PLUGIN_METADATA whose FILE is the CMake-generated package metadata JSON
// (package/module identity + ABI, read by the host via QPluginLoader::metaData()
// without instantiating anything). The host then qobject_casts the instance to
// IPackage and drives each carried module BY ITS MODULE ID.
//
// Package/module identity lives entirely in the JSON authored as
// <Name>.Plugin.json + the referenced .Module.json set; add_framelift_plugin(...)
// merges them into the embedded metadata and defines FRAMELIFT_PACKAGE_METADATA_JSON.
// The C++ macros below only carry the runtime traits that depend on the concrete
// module entry types (the factory, the QObject view-model cast, QML entry, and order).
//
// Single-module package — the common case:
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
//
// Multi-module package — one DLL carrying several modules. Each FRAMELIFT_MODULE id
// must match an id declared in the package's .Module.json set:
//
//   FRAMELIFT_PACKAGE_BEGIN()
//     FRAMELIFT_MODULE("framelift.overlay.core",     OverlayCore,     { .renderOrder = 0 })
//     FRAMELIFT_MODULE("framelift.overlay.settings", OverlaySettings, { .qml = false })
//   FRAMELIFT_PACKAGE_END()

#ifndef FRAMELIFT_PACKAGE_METADATA_JSON
#error                                                                                                                 \
    "FRAMELIFT_PACKAGE_METADATA_JSON must name the CMake-generated package metadata JSON (set by add_framelift_plugin)"
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

// One row per module the package carries: the module id (the dispatch key) plus the
// type-erased factory and QObject view-model cast for that module's concrete entry type. A
// single-module package leaves id null and relies on the count==1 fallback below.
struct ModuleTableEntry
{
    const char* id;
    IModule* (*create)();
    QObject* (*getViewModel)(IModule*);
    const char* qmlEntryUrl;
    int renderOrder;
    bool qml;
};

inline const ModuleTableEntry* FindModule(const ModuleTableEntry* table, int count, const char* id)
{
    if (id)
    {
        for (int i = 0; i < count; ++i)
        {
            if (table[i].id && std::strcmp(table[i].id, id) == 0)
            {
                return &table[i];
            }
        }
    }
    // A single-module package serves its lone module for any requested id, so a
    // host that passes a stale id (or the single-module macro that leaves id null)
    // still resolves correctly.
    return count == 1 ? &table[0] : nullptr;
}
} // namespace framelift::detail

// Shared body: defines the QObject factory class that the host loads via
// QPluginLoader, dispatching IPackage calls against a module table.
#define FRAMELIFT_DETAIL_PACKAGE_FACTORY(Table, Count)                                                                 \
    class FrameLiftPackageFactory final : public QObject, public IPackage                                              \
    {                                                                                                                  \
        Q_OBJECT                                                                                                       \
        Q_PLUGIN_METADATA(IID FrameLiftPackage_IID FILE FRAMELIFT_PACKAGE_METADATA_JSON)                               \
        Q_INTERFACES(IPackage)                                                                                         \
                                                                                                                       \
    public:                                                                                                            \
        void SetLogSink(Log::SinkFn fn) noexcept override                                                              \
        {                                                                                                              \
            Log::SetSink(fn);                                                                                          \
        }                                                                                                              \
        IModule* CreateModule(const char* moduleId) noexcept override                                                  \
        {                                                                                                              \
            const ::framelift::detail::ModuleTableEntry* e =                                                           \
                ::framelift::detail::FindModule((Table), (Count), moduleId);                                           \
            return e ? e->create() : nullptr;                                                                          \
        }                                                                                                              \
        void DestroyModule(IModule* module) noexcept override                                                          \
        {                                                                                                              \
            delete module;                                                                                             \
        }                                                                                                              \
        QObject* GetViewModel(const char* moduleId, IModule* module) noexcept override                                 \
        {                                                                                                              \
            const ::framelift::detail::ModuleTableEntry* e =                                                           \
                ::framelift::detail::FindModule((Table), (Count), moduleId);                                           \
            return (e && e->qml && e->getViewModel) ? e->getViewModel(module) : nullptr;                               \
        }                                                                                                              \
        const char* QmlEntryUrl(const char* moduleId) noexcept override                                                \
        {                                                                                                              \
            const ::framelift::detail::ModuleTableEntry* e =                                                           \
                ::framelift::detail::FindModule((Table), (Count), moduleId);                                           \
            return (e && e->qml) ? e->qmlEntryUrl : nullptr;                                                           \
        }                                                                                                              \
        int RenderOrder(const char* moduleId) noexcept override                                                        \
        {                                                                                                              \
            const ::framelift::detail::ModuleTableEntry* e =                                                           \
                ::framelift::detail::FindModule((Table), (Count), moduleId);                                           \
            return (e && e->qml) ? e->renderOrder : 0;                                                                 \
        }                                                                                                              \
    };

// One table row from a module id, its entry type, and the runtime descriptor. The
// descriptor (FrameLiftModuleEntryDesc) is evaluated as a temporary.
#define FRAMELIFT_MODULE(ModuleId, Type, ...)                                                                          \
    {ModuleId,                                                                                                         \
     &::framelift::detail::CreateModule<Type>,                                                                         \
     &::framelift::detail::GetViewModel<Type>,                                                                         \
     FRAMELIFT_QML_ENTRY_URL,                                                                                          \
     (::FrameLiftModuleEntryDesc __VA_ARGS__).renderOrder,                                                             \
     (::FrameLiftModuleEntryDesc __VA_ARGS__).qml},

#ifdef FRAMELIFT_SUPPRESS_MODULE_ENTRY
#define FRAMELIFT_MODULE_ENTRY(Type, ...)
#define FRAMELIFT_PACKAGE_BEGIN()
#define FRAMELIFT_PACKAGE_END()
#else
// Single-module convenience: builds a one-row table (id left null — the lone module
// is served for any requested id) and emits the factory. The id stays in the JSON
// metadata, so the source carries no module id string.
#define FRAMELIFT_MODULE_ENTRY(Type, ...)                                                                              \
    namespace framelift_module_detail                                                                                  \
    {                                                                                                                  \
    static constexpr ::FrameLiftModuleEntryDesc kDesc = __VA_ARGS__;                                                   \
    static_assert(                                                                                                     \
        !kDesc.qml || std::is_base_of_v<QObject, Type>,                                                                \
        "FRAMELIFT_MODULE_ENTRY: " #Type " is not a QObject; add .qml = false or inherit QObject first"                \
    );                                                                                                                 \
    static const ::framelift::detail::ModuleTableEntry kModuleTable[] = {                                              \
        {nullptr, &::framelift::detail::CreateModule<Type>, &::framelift::detail::GetViewModel<Type>,                  \
         FRAMELIFT_QML_ENTRY_URL, kDesc.renderOrder, kDesc.qml}                                                        \
    };                                                                                                                 \
    }                                                                                                                  \
    FRAMELIFT_DETAIL_PACKAGE_FACTORY(framelift_module_detail::kModuleTable, 1)

// Multi-module package: FRAMELIFT_PACKAGE_BEGIN(), one FRAMELIFT_MODULE(...) per
// carried module, then FRAMELIFT_PACKAGE_END().
#define FRAMELIFT_PACKAGE_BEGIN()                                                                                      \
    namespace framelift_module_detail                                                                                  \
    {                                                                                                                  \
    static const ::framelift::detail::ModuleTableEntry kModuleTable[] = {

#define FRAMELIFT_PACKAGE_END()                                                                                        \
    }                                                                                                                  \
    ;                                                                                                                  \
    static constexpr int kModuleCount = static_cast<int>(sizeof(kModuleTable) / sizeof(kModuleTable[0]));              \
    }                                                                                                                  \
    FRAMELIFT_DETAIL_PACKAGE_FACTORY(framelift_module_detail::kModuleTable, framelift_module_detail::kModuleCount)
#endif
