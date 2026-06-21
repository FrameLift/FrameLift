#pragma once
#include <cstddef>
#include <functional>
#include <framelift/Guard.h>
#include <framelift/IModuleContext.h>
#include <framelift/services/IAppPaths.h>
#include <framelift/services/ISettingsRegistry.h>
#include <framelift/services/ISettingsStore.h>
#include <string>
#include <utility>

// ── Author-side convenience wrappers ─────────────────────────────────────────
// These helpers compile into the plugin (no DLL boundary crossing) and wrap the
// raw fn-ptr+ud ABI into ergonomic std::function / lambda subscriptions.
// They heap-allocate closures and register cleanup callbacks to free them on
// plugin unload (IModuleContext::ClearSubscriptions).

namespace framelift
{

// Subscribe to an event type using a lambda or std::function.
// Closure is heap-allocated and freed automatically when the plugin unloads.
template <typename TEvent, typename Fn>
void Subscribe(IModuleContext& ctx, Fn&& handler)
{
    struct Sub
    {
        std::function<void(const TEvent&)> fn;

        static void call(const void* p, void* ud)
        {
            Guard(
                "event subscriber",
                [&]
                {
                    static_cast<Sub*>(ud)->fn(*static_cast<const TEvent*>(p));
                }
            );
        }

        static void cleanup(void* ud)
        {
            delete static_cast<Sub*>(ud);
        }
    };

    ctx.SubscribeRaw(TEvent::EventId, &Sub::call, new Sub{std::forward<Fn>(handler)}, &Sub::cleanup);
}

// Register a settings-change callback using a lambda.
// Closure is heap-allocated and freed on plugin unload.
template <typename Fn>
void RegisterSettingsChangeCallback(IModuleContext& ctx, Fn&& handler)
{
    struct Closure
    {
        std::function<void()> fn;

        static void call(void* ud)
        {
            Guard(
                "settings-change callback",
                [&]
                {
                    static_cast<Closure*>(ud)->fn();
                }
            );
        }

        static void cleanup(void* ud)
        {
            delete static_cast<Closure*>(ud);
        }
    };

    if (auto* store = ctx.GetService<ISettingsStore>())
    {
        store->RegisterSettingsChangeCallback(
            &Closure::call, new Closure{std::forward<Fn>(handler)}, &Closure::cleanup
        );
    }
}

// Get a setting string into a std::string (allocates, plugin-side only).
inline std::string GetSettingString(const IModuleContext& ctx, const char* key)
{
    const auto* store = ctx.GetService<ISettingsStore>();
    if (!store)
    {
        return {};
    }
    const int len = store->GetSettingString(key, nullptr, 0);
    if (len <= 0)
    {
        return {};
    }
    std::string out(static_cast<std::size_t>(len), '\0');
    store->GetSettingString(key, out.data(), len + 1);
    return out;
}

// Get the pref path as a std::string (allocates, plugin-side only).
inline std::string GetPrefPath(const IModuleContext& ctx)
{
    const auto* paths = ctx.GetService<IAppPaths>();
    if (!paths)
    {
        return {};
    }
    const int len = paths->GetPrefPath(nullptr, 0);
    if (len <= 0)
    {
        return {};
    }
    std::string out(static_cast<std::size_t>(len), '\0');
    paths->GetPrefPath(out.data(), len + 1);
    return out;
}

// Register a keybind backed by a std::string member.
// Avoids having to write the get/set lambdas for each binding.
inline void RegisterKeybindEntry(IModuleContext& ctx, const char* label, const char* actionName, std::string& bindStr)
{
    struct Acc
    {
        static const char* get(void* ud) noexcept
        {
            return static_cast<std::string*>(ud)->c_str();
        }

        static void set(void* ud, const char* val) noexcept
        {
            Guard(
                "keybind setter",
                [&]
                {
                    *static_cast<std::string*>(ud) = val ? val : "";
                }
            );
        }
    };

    if (auto* registry = ctx.GetService<ISettingsRegistry>())
    {
        registry->RegisterKeybindEntry(label, actionName, Acc::get, Acc::set, &bindStr);
    }
}

} // namespace framelift
