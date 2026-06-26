#include "HotkeysImpl.h"
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <framelift/Log.h>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// ── Key parsing & serialisation ───────────────────────────────────────────────

namespace
{
struct KeyBind
{
    Key key;
    Mod mods;
};

std::string_view Trim(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
    {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
    {
        s.remove_suffix(1);
    }
    return s;
}

std::string Lower(std::string_view s)
{
    std::string r(s);
    for (char& c : r)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return r;
}

const std::unordered_map<std::string, Key>& KeyMap()
{
    static const std::unordered_map<std::string, Key> m = {
        {"a", Keys::A},
        {"b", Keys::B},
        {"c", Keys::C},
        {"d", Keys::D},
        {"e", Keys::E},
        {"f", Keys::F},
        {"g", Keys::G},
        {"h", Keys::H},
        {"i", Keys::I},
        {"j", Keys::J},
        {"k", Keys::K},
        {"l", Keys::L},
        {"m", Keys::M},
        {"n", Keys::N},
        {"o", Keys::O},
        {"p", Keys::P},
        {"q", Keys::Q},
        {"r", Keys::R},
        {"s", Keys::S},
        {"t", Keys::T},
        {"u", Keys::U},
        {"v", Keys::V},
        {"w", Keys::W},
        {"x", Keys::X},
        {"y", Keys::Y},
        {"z", Keys::Z},
        {"space", Keys::Space},
        {"return", Keys::Return},
        {"enter", Keys::Enter},
        {"tab", Keys::Tab},
        {"backspace", Keys::Backspace},
        {"escape", Keys::Escape},
        {"esc", Keys::Escape},
        {"right", Keys::Right},
        {"left", Keys::Left},
        {"up", Keys::Up},
        {"down", Keys::Down},
        {"home", Keys::Home},
        {"end", Keys::End},
        {"pageup", Keys::PageUp},
        {"pagedown", Keys::PageDown},
        {"insert", Keys::Insert},
        {"delete", Keys::Delete},
        {"f1", Keys::F1},
        {"f2", Keys::F2},
        {"f3", Keys::F3},
        {"f4", Keys::F4},
        {"f5", Keys::F5},
        {"f6", Keys::F6},
        {"f7", Keys::F7},
        {"f8", Keys::F8},
        {"f9", Keys::F9},
        {"f10", Keys::F10},
        {"f11", Keys::F11},
        {"f12", Keys::F12},
        {"comma", Keys::Comma},
        {",", Keys::Comma},
        {"period", Keys::Period},
        {",", Keys::Comma},
        {".", Keys::Period},
        {"slash", Keys::Slash},
        {"/", Keys::Slash},
        {"backslash", Keys::Backslash},
        {"\\", Keys::Backslash},
        {"leftbracket", Keys::LeftBracket},
        {"[", Keys::LeftBracket},
        {"rightbracket", Keys::RightBracket},
        {"]", Keys::RightBracket},
        {"minus", Keys::Minus},
        {"-", Keys::Minus},
        {"equals", Keys::Equals},
        {"=", Keys::Equals},
        {"apostrophe", Keys::Apostrophe},
        {"'", Keys::Apostrophe},
        {"backtick", Keys::Backtick},
        {"`", Keys::Backtick},
        {"semicolon", Keys::Semicolon},
        {"0", Keys::Num0},
        {"1", Keys::Num1},
        {"2", Keys::Num2},
        {"3", Keys::Num3},
        {"4", Keys::Num4},
        {"5", Keys::Num5},
        {"6", Keys::Num6},
        {"7", Keys::Num7},
        {"8", Keys::Num8},
        {"9", Keys::Num9},
    };
    return m;
}

std::optional<KeyBind> ParseOne(std::string_view raw)
{
    raw = Trim(raw);
    if (raw.empty())
    {
        return {};
    }
    const auto& km = KeyMap();
    auto mods = Mod::None;
    Key key = Keys::Unknown;
    int keyCount = 0;
    std::string_view sv = raw;
    while (!sv.empty())
    {
        const auto plus = sv.find('+');
        const std::string tok = Lower(Trim(sv.substr(0, plus)));
        if (tok == "ctrl")
        {
            mods = mods | Mod::Ctrl;
        }
        else if (tok == "shift")
        {
            mods = mods | Mod::Shift;
        }
        else if (tok == "alt")
        {
            mods = mods | Mod::Alt;
        }
        else
        {
            const auto it = km.find(tok);
            if (it == km.end())
            {
                Log::Error("ParseKeyBind: unknown token '{}'", tok);
                return {};
            }
            key = it->second;
            ++keyCount;
        }
        if (plus == std::string_view::npos)
        {
            break;
        }
        sv.remove_prefix(plus + 1);
    }
    if (keyCount != 1)
    {
        Log::Error("ParseKeyBind: expected 1 key, got {}", keyCount);
        return {};
    }
    return KeyBind{key, mods};
}

std::vector<KeyBind> ParseKeyBindList(std::string_view s)
{
    std::vector<KeyBind> result;
    while (!s.empty())
    {
        const auto semi = s.find(';');
        if (auto kb = ParseOne(s.substr(0, semi)))
        {
            result.push_back(*kb);
        }
        if (semi == std::string_view::npos)
        {
            break;
        }
        s.remove_prefix(semi + 1);
    }
    return result;
}
} // namespace

// KeyBindToString is defined in sdk/src/KeyNames.cpp (shared by host + plugins).

// ── HotkeysImpl ───────────────────────────────────────────────────────────────

void HotkeysImpl::BindRawImpl(
    const std::string& name, Key key, Mod mods, void (*action)(void*), void* ud, void (*cleanup)(void*)
)
{
    // Replace existing named binding
    if (!name.empty())
    {
        for (auto& b : bindings_)
        {
            if (!b.name.empty() && b.name == name)
            {
                if (b.cleanup)
                {
                    b.cleanup(b.ud);
                }
                b.key = key;
                b.mods = mods;
                b.action = action;
                b.ud = ud;
                b.cleanup = cleanup;
                return;
            }
        }
    }
    bindings_.push_back({name, key, mods, action, ud, cleanup});
}

void HotkeysImpl::BindRaw(Key key, Mod mods, void (*action)(void*), void* ud, void (*cleanup)(void*)) noexcept
{
    BindRawImpl({}, key, mods, action, ud, cleanup);
}

void HotkeysImpl::EraseGroup(const std::string& name, bool runCleanup)
{
    std::erase_if(
        bindings_,
        [&](const Binding& b)
        {
            if (b.name.empty() || b.name != name)
            {
                return false;
            }
            if (runCleanup && b.cleanup)
            {
                b.cleanup(b.ud);
            }
            return true;
        }
    );
}

void HotkeysImpl::BindNamedRaw(
    const char* name, const char* bindList, void (*action)(void*), void* ud, void (*cleanup)(void*)
) noexcept
{
    const auto binds = ParseKeyBindList(bindList ? bindList : "");
    if (binds.empty())
    {
        return;
    }
    // Re-binding a name replaces its whole key group. Drop the old group first
    // (freeing its callback) so stale aliases never linger across a rebind.
    const std::string nm = name ? name : "";
    if (!nm.empty())
    {
        EraseGroup(nm, /*runCleanup=*/true);
    }
    // The first key is the primary; the rest share its name as aliases. Only the
    // primary owns the heap callback ud — aliases get cleanup=nullptr so it frees once.
    bindings_.push_back({nm, binds[0].key, binds[0].mods, action, ud, cleanup});
    for (std::size_t i = 1; i < binds.size(); ++i)
    {
        bindings_.push_back({nm, binds[i].key, binds[i].mods, action, ud, nullptr});
    }
}

bool HotkeysImpl::Rebind(const char* name, Key newKey, Mod newMods) noexcept
{
    if (!name)
    {
        return false;
    }
    // Collapse a multi-key action to a single key: update the primary (first match)
    // and drop its aliases so they don't keep firing.
    bool primarySeen = false;
    for (auto& b : bindings_)
    {
        if (!b.name.empty() && b.name == name)
        {
            b.key = newKey;
            b.mods = newMods;
            primarySeen = true;
            break;
        }
    }
    if (!primarySeen)
    {
        return false;
    }
    bool keptPrimary = false;
    std::erase_if(
        bindings_,
        [&](const Binding& b)
        {
            if (b.name.empty() || b.name != name)
            {
                return false;
            }
            if (!keptPrimary)
            {
                keptPrimary = true;
                return false; // keep the primary we just updated
            }
            return true; // drop aliases
        }
    );
    return true;
}

void HotkeysImpl::Unbind(const char* name) noexcept
{
    if (!name)
    {
        return;
    }
    // Removes the whole group (primary + aliases). Matches existing behavior of not
    // running cleanup here — that only happens on Clear().
    EraseGroup(name, /*runCleanup=*/false);
}

void HotkeysImpl::RebindList(const char* name, const char* bindList) noexcept
{
    if (!name)
    {
        return;
    }
    // Reuse the existing action callback so callers (e.g. the settings menu) that
    // don't own it can still swap the whole key set.
    void (*action)(void*) = nullptr;
    void* ud = nullptr;
    void (*cleanup)(void*) = nullptr;
    bool found = false;
    for (const auto& b : bindings_)
    {
        if (!b.name.empty() && b.name == name)
        {
            action = b.action;
            ud = b.ud;
            cleanup = b.cleanup;
            found = true;
            break;
        }
    }
    if (!found)
    {
        return; // nothing bound under this name
    }
    const auto binds = ParseKeyBindList(bindList ? bindList : "");
    // Drop the old group WITHOUT freeing ud — we re-attach it to the new bindings.
    EraseGroup(name, /*runCleanup=*/false);
    if (binds.empty())
    {
        // No keys left: free the now-orphaned callback (we own it here).
        if (cleanup)
        {
            cleanup(ud);
        }
        return;
    }
    const std::string nm = name;
    bindings_.push_back({nm, binds[0].key, binds[0].mods, action, ud, cleanup});
    for (std::size_t i = 1; i < binds.size(); ++i)
    {
        bindings_.push_back({nm, binds[i].key, binds[i].mods, action, ud, nullptr});
    }
}

int HotkeysImpl::GetShortcutString(const char* name, char* buf, int cap) const noexcept
{
    if (!name)
    {
        if (buf && cap > 0)
        {
            buf[0] = '\0';
        }
        return 0;
    }
    for (const auto& b : bindings_)
    {
        if (!b.name.empty() && b.name == name)
        {
            char tmp[64] = {};
            KeyBindToString(b.key, b.mods, tmp, sizeof(tmp));
            const int len = static_cast<int>(std::strlen(tmp));
            if (buf && cap > 0)
            {
                const int n = len < cap - 1 ? len : cap - 1;
                std::memcpy(buf, tmp, static_cast<std::size_t>(n));
                buf[n] = '\0';
            }
            return len;
        }
    }
    if (buf && cap > 0)
    {
        buf[0] = '\0';
    }
    return 0;
}

void HotkeysImpl::Clear() noexcept
{
    for (auto& b : bindings_)
    {
        if (b.cleanup)
        {
            b.cleanup(b.ud);
        }
    }
    bindings_.clear();
}

bool HotkeysImpl::Handle(const AppEvent& e) const noexcept
{
    if (e.type != AppEventType::KeyDown)
    {
        return false;
    }
    const AppEvent::KeyPayload& kp = e.AsKey();
    for (const auto& b : bindings_)
    {
        if (b.key != kp.key)
        {
            continue;
        }
        const bool modsMatch = b.mods == Mod::None ? kp.mods == Mod::None : kp.mods == b.mods;
        if (modsMatch && b.action)
        {
            b.action(b.ud);
            return true;
        }
    }
    return false;
}
