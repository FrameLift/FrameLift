#pragma once
#include <framelift/IModule.h>
#include <framelift/IRenderable.h>
#include <framelift/ModuleABI.h>
#include <string>
#include <unordered_set>
#include <vector>

// Loads every package DLL present in a directory. A package carries one or more
// modules; each module is instantiated by its id unless that id is in the disabled
// set. Dependency order between packages is resolved from embedded metadata.
// Each DLL must export the six framelift_* C symbols below.
//
// extern "C" {
//   const FrameLiftPackageInfo* framelift_module_info();
//   void         framelift_set_log_sink(Log::SinkFn);
//   IModule*     framelift_create(const char* moduleId);
//   void         framelift_destroy(IModule*);
//   IRenderable* framelift_get_renderable(const char* moduleId, IModule*); // nullptr if not renderable
//   int          framelift_render_order(const char* moduleId);              // z-order for renderables
// }
class PackageLoader
{
public:
    // One instantiated module within a loaded package.
    struct LoadedModule
    {
        std::string moduleId;
        IModule* module;
        IRenderable* renderable; // may be nullptr
        int renderOrder;
    };

    struct LoadedPackage
    {
        std::string name;       // package id / enabled-list entry
        std::string moduleFile; // shipped package binary filename.
        void* handle;           // HMODULE on Windows, dlopen handle on POSIX
        void (*destroyFn)(IModule*);
        const FrameLiftPackageInfo* info; // identity descriptor (points into the loaded DLL)
        std::vector<LoadedModule> modules; // every module instantiated from this package
    };

    // Module identity copied out of a present-but-not-loaded package's metadata, so
    // the settings UI can list and re-enable individual modules even when the DLL is
    // not loaded (the lib is closed after discovery — these strings are owned copies).
    struct AvailableModule
    {
        std::string id;
        std::string name;
        std::string description;
    };

    struct AvailablePackage
    {
        std::string packageId;
        std::string moduleFile;
        std::string displayName; // info->name, or moduleFile when metadata is unreadable
        int version[3] = {0, 0, 0};
        std::string publisher;
        std::string description;
        std::vector<AvailableModule> modules;
    };

    // Scans packages/ for shared libraries and loads every ABI-compatible package.
    // Within each, every module whose id is NOT in `disabledModules` is instantiated.
    // Dependencies and load order are resolved from embedded package/module metadata
    // before any module object is created. Does NOT call Install(); the caller does
    // that via Registry().Add(module, ctx).
    void LoadAll(const std::string& modulesDir, const std::unordered_set<std::string>& disabledModules = {});

    const std::vector<LoadedPackage>& Packages() const
    {
        return packages_;
    }

    // Discover every package binary present in modulesDir (with full identity and its
    // module list), so the settings UI can list and re-enable packages/modules that
    // are currently disabled or failed to load.
    static std::vector<AvailablePackage> DiscoverAvailable(const std::string& modulesDir);

    // Calls framelift_destroy for every instantiated module and FreeLibrary per package.
    ~PackageLoader();

private:
    std::vector<LoadedPackage> packages_;
};
