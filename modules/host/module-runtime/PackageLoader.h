#pragma once
#include <framelift/IModule.h>
#include <framelift/IRenderable.h>
#include <framelift/ModuleABI.h>
#include <string>
#include <unordered_set>
#include <vector>

// Loads every module DLL present in a directory. Enablement is driven by module
// JSON (a disabled package isn't built/shipped, so it's absent), and dependency
// order is resolved from embedded metadata.
// Each DLL must export the four framelift_* C symbols below.
//
// extern "C" {
//   IModule*    framelift_create();
//   void         framelift_destroy(IModule*);
//   IRenderable* framelift_get_renderable(IModule*);   // nullptr if not renderable
//   int          framelift_render_order();               // z-order for renderables
// }
class PackageLoader
{
public:
    struct LoadedPackage
    {
        std::string name;          // package id / enabled-list entry
        std::string moduleFile; // shipped module binary filename.
        void* handle; // HMODULE on Windows, dlopen handle on POSIX
        IModule* module;
        IRenderable* renderable; // may be nullptr
        int renderOrder;
        void (*destroyFn)(IModule*);
        const FrameLiftPackageInfo* info; // identity descriptor (points into the loaded DLL)
    };

    struct AvailablePackage
    {
        std::string packageId;
        std::string moduleFile;
    };

    // Scans Modules/ for shared libraries and loads every ABI-compatible package
    // whose id is not in `disabled`. Dependencies and load order are resolved from
    // embedded package/module metadata before any module object is created.
    // Does NOT call Install(); the caller does that via Registry().Add(p, ctx).
    void LoadAll(const std::string& modulesDir, const std::unordered_set<std::string>& disabled = {});

    const std::vector<LoadedPackage>& Packages() const
    {
        return packages_;
    }

    // Discover every module binary present in modulesDir, so the settings UI can
    // list and re-enable packages that are currently disabled.
    static std::vector<AvailablePackage> DiscoverAvailable(const std::string& modulesDir);

    // Calls framelift_destroy and FreeLibrary for every loaded package.
    ~PackageLoader();

private:
    std::vector<LoadedPackage> packages_;
};
