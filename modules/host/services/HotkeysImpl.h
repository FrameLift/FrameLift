#pragma once
#include <functional>
#include <framelift/Hotkeys.h>
#include <string>
#include <utility>
#include <vector>

// Concrete host-side Hotkeys implementation.
// The host creates one HotkeysImpl instance; App passes it to plugins as Hotkeys&.
class HotkeysImpl final : public Hotkeys
{
public:
    void BindRaw(Key key, Mod mods, void (*action)(void*), void* ud, void (*cleanup)(void*)) noexcept override;

    void BindNamedRaw(
        const char* name, const char* bindList, void (*action)(void*), void* ud, void (*cleanup)(void*)
    ) noexcept override;

    bool Rebind(const char* name, Key newKey, Mod newMods) noexcept override;
    void Unbind(const char* name) noexcept override;

    int GetShortcutString(const char* name, char* buf, int cap) const noexcept override;
    void Clear() noexcept override;
    bool Handle(const AppEvent& e) const noexcept override;

private:
    struct Binding
    {
        std::string name; // empty = unnamed
        Key key = 0;
        Mod mods = Mod::None;
        void (*action)(void*) = nullptr;
        void* ud = nullptr;
        void (*cleanup)(void*) = nullptr;
    };

    std::vector<Binding> bindings_;

    void BindRawImpl(
        const std::string& name, Key key, Mod mods, void (*action)(void*), void* ud, void (*cleanup)(void*)
    );
};

// Free-function helper (non-virtual, std::function wrapper) for host use only.
// Plugins use framelift::Bind() helpers from HotkeyHelpers.h.
namespace host
{
template <typename Fn>
inline void Bind(HotkeysImpl& keys, Key key, Mod mods, Fn&& fn)
{
    struct C
    {
        std::function<void()> fn;
    };

    auto* c = new C{std::forward<Fn>(fn)};
    keys.BindRaw(
        key, mods,
        [](void* ud)
        {
            static_cast<C*>(ud)->fn();
        },
        c,
        [](void* ud)
        {
            delete static_cast<C*>(ud);
        }
    );
}

template <typename Fn>
inline void Bind(HotkeysImpl& keys, Key key, Fn&& fn)
{
    Bind(keys, key, Mod::None, std::forward<Fn>(fn));
}

template <typename Fn>
inline void Bind(HotkeysImpl& keys, const char* name, const char* bindList, Fn&& fn)
{
    struct C
    {
        std::function<void()> fn;
    };

    auto* c = new C{std::forward<Fn>(fn)};
    keys.BindNamedRaw(
        name, bindList,
        [](void* ud)
        {
            static_cast<C*>(ud)->fn();
        },
        c,
        [](void* ud)
        {
            delete static_cast<C*>(ud);
        }
    );
}

template <typename Fn>
inline void Bind(HotkeysImpl& keys, const char* name, const std::string& bindList, Fn&& fn)
{
    Bind(keys, name, bindList.c_str(), std::forward<Fn>(fn));
}
} // namespace host
