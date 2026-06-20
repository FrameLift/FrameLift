#pragma once
#include <framelift/ModuleABI.h>

// The package catalogue: enumerate every package discovered in the Modules/
// directory (loaded or present-but-disabled) and toggle enablement. A capability
// service — discover it with ctx.GetService<IPackageCatalog>().
class IPackageCatalog
{
public:
    static constexpr const char* InterfaceId = "framelift.IPackageCatalog";
    virtual ~IPackageCatalog() = default;

    //   name    — the load key (package id / enabled entry); use it for SetPackageEnabled.
    //   info    — full descriptor when loaded == true; when loaded == false only
    //             info.name is meaningful (equals name), the rest is zero/null.
    //   enabled    — whether the package is in the persisted enabled list (live).
    //   loaded     — whether the package is currently loaded this session.
    //   loadFailed — true iff it was enabled at startup but did not load (a real error).
    // The `info` reference and every `const char*` it exposes are valid ONLY for the
    // duration of this call — for a present-but-disabled package `info` is a stack
    // temporary. Copy anything you need; never cache the reference or its strings.
    virtual void EnumeratePackages(
        void (*visit)(
            const char* name, const FrameLiftPackageInfo& info, bool enabled, bool loaded, bool loadFailed, void* visitUd
        ),
        void* visitUd
    ) const noexcept = 0;

    // Add/remove a package from the persisted enabled list and save. Unknown names
    // are ignored. The change takes effect on the next application start.
    virtual void SetPackageEnabled(const char* name, bool enabled) noexcept = 0;
};
