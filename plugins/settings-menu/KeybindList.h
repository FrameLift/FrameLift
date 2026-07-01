#pragma once
#include <cstddef>
#include <string>
#include <vector>

// Helpers for the ';'-separated keybind list format (e.g. "Ctrl+F;F2") used by the
// Settings → Keybinds editor. Header-only and inline so SettingsMenu and its unit
// tests share one definition. Tokens are compared verbatim — KeyBindToString output
// and the hand-written CoreSettings defaults are both already canonical.
namespace keybinds
{
// Split into trimmed, non-empty key tokens.
inline std::vector<std::string> Split(const std::string& list)
{
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= list.size())
    {
        const std::size_t semi = list.find(';', start);
        const std::size_t end = semi == std::string::npos ? list.size() : semi;
        std::size_t a = start;
        std::size_t b = end;
        while (a < b && static_cast<unsigned char>(list[a]) <= ' ')
        {
            ++a;
        }
        while (b > a && static_cast<unsigned char>(list[b - 1]) <= ' ')
        {
            --b;
        }
        if (b > a)
        {
            out.emplace_back(list.substr(a, b - a));
        }
        if (semi == std::string::npos)
        {
            break;
        }
        start = semi + 1;
    }
    return out;
}

inline bool Contains(const std::string& list, const std::string& key)
{
    for (const auto& k : Split(list))
    {
        if (k == key)
        {
            return true;
        }
    }
    return false;
}

// Re-join the non-empty tokens with ';'.
inline std::string Join(const std::vector<std::string>& keys)
{
    std::string out;
    for (const auto& k : keys)
    {
        if (k.empty())
        {
            continue;
        }
        if (!out.empty())
        {
            out += ';';
        }
        out += k;
    }
    return out;
}

// The key occupying slot `index` (0 = primary, 1 = alternate), or "" if unset.
inline std::string Slot(const std::string& list, int index)
{
    const std::vector<std::string> keys = Split(list);
    return index >= 0 && index < static_cast<int>(keys.size()) ? keys[index] : std::string{};
}

// Set slot `index` to `key` (empty clears it). The UI supports `slotCount` keys per
// action (2). If `key` already sits in the other slot it's removed there so the
// same key never lands twice. Returns the compacted ';'-list.
inline std::string SetSlot(const std::string& list, int index, const std::string& key, int slotCount = 2)
{
    std::vector<std::string> keys = Split(list);
    keys.resize(static_cast<std::size_t>(slotCount));
    if (!key.empty())
    {
        for (int i = 0; i < slotCount; ++i)
        {
            if (i != index && keys[static_cast<std::size_t>(i)] == key)
            {
                keys[static_cast<std::size_t>(i)].clear();
            }
        }
    }
    if (index >= 0 && index < slotCount)
    {
        keys[static_cast<std::size_t>(index)] = key;
    }
    return Join(keys);
}
} // namespace keybinds
