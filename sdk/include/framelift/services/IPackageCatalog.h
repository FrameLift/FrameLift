#pragma once
#include <framelift/ModuleABI.h>

// The package catalogue: enumerate every package discovered in the packages/
// directory (loaded or present-but-disabled), enumerate the modules each package
// carries, and toggle individual modules. A capability service — discover it with
// ctx.GetService<IPackageCatalog>().
//
// Enablement is per MODULE: one package DLL may carry several modules, each toggled
// independently. EnumeratePackages drives the per-package header; EnumerateModules
// drives the per-module toggles nested under it (grouped by packageId).
class IPackageCatalog
{
public:
    static constexpr const char* InterfaceId = "framelift.IPackageCatalog";
    virtual ~IPackageCatalog() = default;

    // Per-package identity, in catalogue order.
    //   packageId   — the package's load key
    //   displayName — human-readable package name (equals packageId if unnamed)
    //   version     — pointer to {major, minor, patch}; {0,0,0} if metadata unreadable
    //   publisher   — vendor/author, or "" if unset
    //   description — one-line summary, or "" if unset
    //   loaded      — at least one of the package's modules is loaded this session
    // Every const char* (and the version pointer) is valid ONLY for the duration of
    // this call — copy anything you need; never cache the pointers.
    virtual void EnumeratePackages(
        void (*visit)(
            const char* packageId, const char* displayName, const int* version, const char* publisher,
            const char* description, bool loaded, void* visitUd
        ),
        void* visitUd
    ) const noexcept = 0;

    // Per-module enablement — the toggle unit. Visits every module of every package,
    // grouped by package (in EnumeratePackages order).
    //   packageId   — owning package's load key
    //   moduleId    — module id (the key for SetModuleEnabled)
    //   moduleName  — human-readable module name (equals moduleId if unnamed)
    //   description — one-line module summary, or "" if unset
    //   enabled     — persisted enable state (live; reflects toggles immediately)
    //   loaded      — instantiated this session
    //   loadFailed  — true iff it was enabled at startup yet did not load (a real error)
    virtual void EnumerateModules(
        void (*visit)(
            const char* packageId, const char* moduleId, const char* moduleName, const char* description, bool enabled,
            bool loaded, bool loadFailed, void* visitUd
        ),
        void* visitUd
    ) const noexcept = 0;

    // Add/remove a single module from the persisted enabled list and save. Unknown
    // ids are ignored. The change takes effect on the next application start.
    virtual void SetModuleEnabled(const char* moduleId, bool enabled) noexcept = 0;
};
