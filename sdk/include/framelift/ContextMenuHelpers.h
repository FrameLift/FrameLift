#pragma once

#include <framelift/ContextMenu.h>
#include <framelift/Guard.h>
#include <functional>
#include <string>
#include <utility>

namespace framelift
{

template <typename Fn>
inline void AddItem(ContextMenu& menu, const char* label, Fn&& fn)
{
    struct C
    {
        std::function<void()> fn;

        static void call(void* ud)
        {
            Guard(
                "context-menu action",
                [&]
                {
                    static_cast<C*>(ud)->fn();
                }
            );
        }

        static void cleanup(void* ud)
        {
            delete static_cast<C*>(ud);
        }
    };

    menu.AddItemRaw(label, C::call, new C{std::forward<Fn>(fn)}, C::cleanup);
}

template <typename Fn>
inline void AddItem(ContextMenu& menu, const char* label, const char* hotkey, Fn&& fn)
{
    struct C
    {
        std::function<void()> fn;

        static void call(void* ud)
        {
            Guard(
                "context-menu action",
                [&]
                {
                    static_cast<C*>(ud)->fn();
                }
            );
        }

        static void cleanup(void* ud)
        {
            delete static_cast<C*>(ud);
        }
    };

    menu.AddItemWithHotkeyRaw(label, hotkey, C::call, new C{std::forward<Fn>(fn)}, C::cleanup);
}

template <typename Fn>
inline void AddItem(ContextMenu& menu, std::string label, Fn&& fn)
{
    AddItem(menu, label.c_str(), std::forward<Fn>(fn));
}

template <typename Fn>
inline void AddItem(ContextMenu& menu, std::string label, std::string hotkey, Fn&& fn)
{
    AddItem(menu, label.c_str(), hotkey.c_str(), std::forward<Fn>(fn));
}

template <typename BuilderFn>
inline void AddSection(ContextMenu& menu, BuilderFn&& builder)
{
    struct C
    {
        std::function<void(ContextMenu&)> fn;

        static void build(ContextMenu& m, void* ud)
        {
            Guard(
                "context-menu section",
                [&]
                {
                    static_cast<C*>(ud)->fn(m);
                }
            );
        }

        static void cleanup(void* ud)
        {
            delete static_cast<C*>(ud);
        }
    };

    menu.AddSectionRaw(C::build, new C{std::forward<BuilderFn>(builder)}, C::cleanup);
}

} // namespace framelift
