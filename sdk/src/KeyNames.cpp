#include <framelift/Hotkeys.h>
#include <unordered_map>

// KeyBindToString lives in the SDK source set (compiled into every plugin AND the
// host) because both sides need to render a Key+Mod as a display string — e.g.
// SettingsMenu shows a freshly-captured binding. The key-name table is duplicated
// per module, which is fine: it is read-only static data.

namespace
{
const std::unordered_map<Key, const char*>& CanonicalKeyNames()
{
    static const std::unordered_map<Key, const char*> m = {
        {Keys::A, "A"},
        {Keys::B, "B"},
        {Keys::C, "C"},
        {Keys::D, "D"},
        {Keys::E, "E"},
        {Keys::F, "F"},
        {Keys::G, "G"},
        {Keys::H, "H"},
        {Keys::I, "I"},
        {Keys::J, "J"},
        {Keys::K, "K"},
        {Keys::L, "L"},
        {Keys::M, "M"},
        {Keys::N, "N"},
        {Keys::O, "O"},
        {Keys::P, "P"},
        {Keys::Q, "Q"},
        {Keys::R, "R"},
        {Keys::S, "S"},
        {Keys::T, "T"},
        {Keys::U, "U"},
        {Keys::V, "V"},
        {Keys::W, "W"},
        {Keys::X, "X"},
        {Keys::Y, "Y"},
        {Keys::Z, "Z"},
        {Keys::Space, "Space"},
        {Keys::Return, "Return"},
        {Keys::Enter, "Enter"},
        {Keys::Tab, "Tab"},
        {Keys::Backspace, "Backspace"},
        {Keys::Escape, "Escape"},
        {Keys::Right, "Right"},
        {Keys::Left, "Left"},
        {Keys::Up, "Up"},
        {Keys::Down, "Down"},
        {Keys::Home, "Home"},
        {Keys::End, "End"},
        {Keys::PageUp, "PageUp"},
        {Keys::PageDown, "PageDown"},
        {Keys::Insert, "Insert"},
        {Keys::Delete, "Delete"},
        {Keys::F1, "F1"},
        {Keys::F2, "F2"},
        {Keys::F3, "F3"},
        {Keys::F4, "F4"},
        {Keys::F5, "F5"},
        {Keys::F6, "F6"},
        {Keys::F7, "F7"},
        {Keys::F8, "F8"},
        {Keys::F9, "F9"},
        {Keys::F10, "F10"},
        {Keys::F11, "F11"},
        {Keys::F12, "F12"},
        {Keys::Comma, "Comma"},
        {Keys::Period, "Period"},
        {Keys::Slash, "Slash"},
        {Keys::Backslash, "Backslash"},
        {Keys::LeftBracket, "LeftBracket"},
        {Keys::RightBracket, "RightBracket"},
        {Keys::Minus, "Minus"},
        {Keys::Equals, "Equals"},
        {Keys::Apostrophe, "Apostrophe"},
        {Keys::Backtick, "Backtick"},
        {Keys::Semicolon, "Semicolon"},
        {Keys::Num0, "0"},
        {Keys::Num1, "1"},
        {Keys::Num2, "2"},
        {Keys::Num3, "3"},
        {Keys::Num4, "4"},
        {Keys::Num5, "5"},
        {Keys::Num6, "6"},
        {Keys::Num7, "7"},
        {Keys::Num8, "8"},
        {Keys::Num9, "9"},
    };
    return m;
}
} // namespace

const char* KeyBindToString(Key key, Mod mods, char* buf, int cap) noexcept
{
    if (!buf || cap <= 0)
    {
        return "";
    }
    int pos = 0;
    auto append = [&](const char* s)
    {
        while (*s && pos < cap - 1)
        {
            buf[pos++] = *s++;
        }
    };
    if (ModSet(mods, Mod::Ctrl))
    {
        append("Ctrl+");
    }
    if (ModSet(mods, Mod::Shift))
    {
        append("Shift+");
    }
    if (ModSet(mods, Mod::Alt))
    {
        append("Alt+");
    }
    const auto& cn = CanonicalKeyNames();
    const auto it = cn.find(key);
    append(it != cn.end() ? it->second : "?");
    buf[pos] = '\0';
    return buf;
}
