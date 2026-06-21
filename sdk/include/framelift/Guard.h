#pragma once
#include <exception>
#include <framelift/Log.h>
#include <type_traits>

namespace framelift
{
// Run fn in try/catch on the plugin side of the ABI boundary. A throw is
// logged and swallowed (value-returning callables fall back to R{}): the
// plugin misbehaves loudly instead of taking the host down via the noexcept
// boundary → std::terminate. Used by the SDK scaffolding (ModuleBase, Panel,
// SafeRenderable, the helper-thunk trampolines) at every point where the host
// calls back into plugin code.
template <typename Fn>
auto Guard(const char* what, Fn&& fn) noexcept -> decltype(fn())
{
    using R = decltype(fn());
    try
    {
        return fn();
    }
    catch (const std::exception& e)
    {
        Log::Error("Unhandled exception in {}: {}", what, e.what());
    }
    catch (...)
    {
        Log::Error("Unhandled non-standard exception in {}", what);
    }
    if constexpr (!std::is_void_v<R>)
    {
        return R{};
    }
}

// Two-part label variant ("<plugin>.<site>") — avoids building a std::string
// label on hot paths; the parts are only formatted when a throw is caught.
template <typename Fn>
auto Guard(const char* plugin, const char* site, Fn&& fn) noexcept -> decltype(fn())
{
    using R = decltype(fn());
    try
    {
        return fn();
    }
    catch (const std::exception& e)
    {
        Log::Error("Unhandled exception in {}.{}: {}", plugin, site, e.what());
    }
    catch (...)
    {
        Log::Error("Unhandled non-standard exception in {}.{}", plugin, site);
    }
    if constexpr (!std::is_void_v<R>)
    {
        return R{};
    }
}
} // namespace framelift
