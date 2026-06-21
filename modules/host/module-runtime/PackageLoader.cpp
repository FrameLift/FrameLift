#include "PackageLoader.h"
#include "PackageResolver.h"
#include <chrono>
#include <filesystem>
#include <framelift/Log.h>
#include <framelift/ModuleABI.h>
#include <ranges>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

Log::SinkFn HostLogSink();

namespace
{
#ifdef _WIN32
constexpr const char* kPackageExt = ".dll";

void* OpenLib(const char* path)
{
    return LoadLibraryA(path);
}

void CloseLib(void* h)
{
    FreeLibrary(static_cast<HMODULE>(h));
}

void* FindSym(void* h, const char* name)
{
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(h), name));
}

std::string LastLoadError()
{
    return std::to_string(GetLastError());
}
#else
constexpr const char* kPackageExt = ".so";

void* OpenLib(const char* path)
{
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
}

void CloseLib(void* h)
{
    dlclose(h);
}

void* FindSym(void* h, const char* name)
{
    return dlsym(h, name);
}

std::string LastLoadError()
{
    const char* err = dlerror();
    return err ? err : "unknown error";
}
#endif

template <typename Fn>
Fn LoadSym(void* mod, const char* name)
{
    return reinterpret_cast<Fn>(FindSym(mod, name));
}

struct PackageBinary
{
    std::string moduleFile;
    std::string path;
};

struct PackageCandidate
{
    PackageBinary binary;
    void* handle = nullptr;
    const FrameLiftPackageInfo* info = nullptr;
};

std::vector<PackageBinary> DiscoverPackageBinaries(const std::string& modulesDir)
{
    namespace fs = std::filesystem;
    std::vector<PackageBinary> out;

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(modulesDir, ec))
    {
        std::error_code fec;
        if (!entry.is_regular_file(fec) || entry.path().extension() != kPackageExt)
        {
            continue;
        }

        out.push_back({entry.path().filename().string(), entry.path().string()});
    }

    return out;
}

const FrameLiftPackageInfo* ReadPackageInfo(void* handle)
{
    using PackageInfoFn = const FrameLiftPackageInfo* (*)();
    const auto packageInfoFn = LoadSym<PackageInfoFn>(handle, "framelift_module_info");
    return packageInfoFn ? packageInfoFn() : nullptr;
}

bool AbiCompatible(const FrameLiftPackageInfo* info)
{
    return info && FrameLiftAbiCompatible(info->abiVersion, FRAMELIFT_ABI_VERSION);
}

std::string PackageId(const FrameLiftPackageInfo* info)
{
    return info && info->packageId ? info->packageId : "<unknown>";
}
} // namespace

void PackageLoader::LoadAll(const std::string& modulesDir, const std::unordered_set<std::string>& disabledModules)
{
    using Clock = std::chrono::steady_clock;
    const auto loadStart = Clock::now();

    // Open and ABI-check every package present in the directory (dedupe by id).
    std::vector<PackageCandidate> candidates;
    std::unordered_map<std::string, std::size_t> seenIds;
    for (const auto& binary : DiscoverPackageBinaries(modulesDir))
    {
        void* const handle = OpenLib(binary.path.c_str());
        if (!handle)
        {
            Log::Warn("Package '{}': metadata load failed ({})", binary.moduleFile, LastLoadError());
            continue;
        }

        const FrameLiftPackageInfo* const info = ReadPackageInfo(handle);
        if (!info)
        {
            Log::Warn("Package '{}': missing framelift_module_info - rebuild against current SDK", binary.moduleFile);
            CloseLib(handle);
            continue;
        }
        if (!AbiCompatible(info))
        {
            Log::Warn(
                "Package '{}' v{}.{}.{}: ABI version {} incompatible with host version {} - rebuild against current SDK",
                PackageId(info), info->version[0], info->version[1], info->version[2], info->abiVersion, FRAMELIFT_ABI_VERSION
            );
            CloseLib(handle);
            continue;
        }
        if (seenIds.contains(PackageId(info)))
        {
            Log::Warn("Package '{}': duplicate package id - skipped", PackageId(info));
            CloseLib(handle);
            continue;
        }
        seenIds.emplace(PackageId(info), candidates.size());
        candidates.push_back(PackageCandidate{binary, handle, info});
    }

    // Resolve dependencies over every discovered package, then load the accepted
    // ones in dependency order (providers before consumers).
    std::vector<PackageResolveCandidate> resolveCandidates;
    resolveCandidates.reserve(candidates.size());
    for (const auto& candidate : candidates)
    {
        resolveCandidates.push_back({candidate.info});
    }

    const std::vector<PackageResolveDecision> decisions =
        ResolvePackages(resolveCandidates, FrameLiftCurrentPlatformId());

    std::vector<PackageResolveCandidate> acceptedResolve;
    std::vector<std::size_t> acceptedIndex;
    for (std::size_t i = 0; i < candidates.size(); ++i)
    {
        if (decisions[i].accepted)
        {
            acceptedResolve.push_back({candidates[i].info});
            acceptedIndex.push_back(i);
        }
        else
        {
            Log::Warn("Package '{}': {} - skipped", PackageId(candidates[i].info), decisions[i].reason);
            CloseLib(candidates[i].handle);
            candidates[i].handle = nullptr;
        }
    }

    // Diagnose colliding providers: two accepted packages declaring the same module
    // id or feature. We keep first-wins (load order decides the winner) but warn so
    // the collision is visible rather than silent.
    {
        std::unordered_map<std::string, std::string> firstProvider; // token -> package id
        const auto note = [&](const char* token, const char* kind, const char* owner)
        {
            if (!token || !token[0])
            {
                return;
            }
            const auto [it, inserted] = firstProvider.emplace(token, owner);
            if (!inserted)
            {
                Log::Warn(
                    "Package '{}': {} '{}' already provided by '{}' - keeping the first provider", owner, kind, token,
                    it->second
                );
            }
        };
        for (const auto& accepted : acceptedResolve)
        {
            const FrameLiftPackageInfo* info = accepted.info;
            const std::string owner = PackageId(info);
            for (int m = 0; m < info->moduleCount; ++m)
            {
                const FrameLiftModuleInfo& module = info->modules[m];
                note(module.id, "module", owner.c_str());
                for (int f = 0; f < module.providesFeatures.count; ++f)
                {
                    note(
                        module.providesFeatures.items ? module.providesFeatures.items[f] : nullptr, "feature",
                        owner.c_str()
                    );
                }
            }
        }
    }

    for (const std::size_t orderIdx : OrderPackages(acceptedResolve))
    {
        PackageCandidate& candidate = candidates[acceptedIndex[orderIdx]];
        const FrameLiftPackageInfo* const info = candidate.info;

        const auto packageStart = Clock::now();
        using CreateFn = IModule* (*)(const char*);
        using DestroyFn = void (*)(IModule*);
        using GetRenderFn = IRenderable* (*)(const char*, IModule*);
        using RenderOrderFn = int (*)(const char*);

        const auto createFn = LoadSym<CreateFn>(candidate.handle, "framelift_create");
        const auto destroyFn = LoadSym<DestroyFn>(candidate.handle, "framelift_destroy");
        const auto getRenderFn = LoadSym<GetRenderFn>(candidate.handle, "framelift_get_renderable");
        const auto renderOrderFn = LoadSym<RenderOrderFn>(candidate.handle, "framelift_render_order");

        if (!createFn || !destroyFn || !getRenderFn || !renderOrderFn)
        {
            Log::Warn("Package '{}': missing required exports - skipped", PackageId(info));
            CloseLib(candidate.handle);
            candidate.handle = nullptr;
            continue;
        }

        using SetLogSinkFn = void (*)(Log::SinkFn);
        if (const auto setLogSinkFn = LoadSym<SetLogSinkFn>(candidate.handle, "framelift_set_log_sink"))
        {
            setLogSinkFn(HostLogSink());
        }

        // Instantiate every module the package carries whose id the user hasn't
        // disabled. A package may end up loading some of its modules and not others.
        std::vector<PackageLoader::LoadedModule> loadedModules;
        for (int m = 0; m < info->moduleCount; ++m)
        {
            const char* const moduleId = info->modules[m].id;
            const std::string moduleKey = moduleId ? moduleId : std::string();
            if (disabledModules.contains(moduleKey))
            {
                Log::Info("Module '{}': disabled by user - skipped", moduleKey);
                continue;
            }

            IModule* module = createFn(moduleId);
            if (!module)
            {
                Log::Warn("Module '{}': framelift_create() returned nullptr - skipped", moduleKey);
                continue;
            }

            const int order = renderOrderFn(moduleId);
            IRenderable* renderable = getRenderFn(moduleId, module);
            loadedModules.push_back({moduleKey, module, renderable, order});
        }

        if (loadedModules.empty())
        {
            // Every module disabled (or each failed to construct): the DLL stays
            // loaded for nothing, so drop it. It still shows in the settings UI via
            // DiscoverAvailable so the user can re-enable a module.
            Log::Info("Package '{}': no enabled modules - not loaded", PackageId(info));
            CloseLib(candidate.handle);
            candidate.handle = nullptr;
            continue;
        }

        packages_.push_back(
            {PackageId(info), candidate.binary.moduleFile, candidate.handle, destroyFn, info, std::move(loadedModules)}
        );
        candidate.handle = nullptr; // ownership moved to packages_

        const std::string by = info->publisher ? std::string(" by ") + info->publisher : std::string();
        const double packageMs = std::chrono::duration<double, std::milli>(Clock::now() - packageStart).count();
        Log::Info(
            "Package '{}' v{}.{}.{}{} loaded (abi version {}, {} of {} module(s), {:.1f} ms)", PackageId(info),
            info->version[0], info->version[1], info->version[2], by, info->abiVersion, packages_.back().modules.size(),
            info->moduleCount, packageMs
        );
    }

    for (auto& candidate : candidates)
    {
        if (candidate.handle)
        {
            CloseLib(candidate.handle);
        }
    }

    const double totalMs = std::chrono::duration<double, std::milli>(Clock::now() - loadStart).count();
    Log::Info("Loaded {} package(s) in {:.1f} ms", packages_.size(), totalMs);
}

std::vector<PackageLoader::AvailablePackage> PackageLoader::DiscoverAvailable(const std::string& modulesDir)
{
    std::vector<AvailablePackage> out;
    for (const auto& binary : DiscoverPackageBinaries(modulesDir))
    {
        void* const handle = OpenLib(binary.path.c_str());
        if (!handle)
        {
            out.push_back({binary.moduleFile, binary.moduleFile, binary.moduleFile});
            continue;
        }

        const FrameLiftPackageInfo* const info = ReadPackageInfo(handle);
        if (info && AbiCompatible(info) && info->packageId)
        {
            // Copy every field the catalogue needs out of the descriptor before the
            // DLL is closed — the pointers below are invalid after CloseLib.
            AvailablePackage pkg;
            pkg.packageId = info->packageId;
            pkg.moduleFile = binary.moduleFile;
            pkg.displayName = info->name ? info->name : info->packageId;
            pkg.version[0] = info->version[0];
            pkg.version[1] = info->version[1];
            pkg.version[2] = info->version[2];
            pkg.publisher = info->publisher ? info->publisher : "";
            pkg.description = info->description ? info->description : "";
            for (int m = 0; m < info->moduleCount; ++m)
            {
                const FrameLiftModuleInfo& mod = info->modules[m];
                pkg.modules.push_back(
                    {mod.id ? mod.id : "", mod.name ? mod.name : "", mod.description ? mod.description : ""}
                );
            }
            out.push_back(std::move(pkg));
        }
        else
        {
            out.push_back({binary.moduleFile, binary.moduleFile, binary.moduleFile});
        }
        CloseLib(handle);
    }
    return out;
}

PackageLoader::~PackageLoader()
{
    for (const auto& p : packages_)
    {
        for (const auto& m : p.modules)
        {
            p.destroyFn(m.module);
        }
        CloseLib(p.handle);
    }
}
