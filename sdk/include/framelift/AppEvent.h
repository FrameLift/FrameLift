#pragma once

#include <cstdint>

// ── Keyboard ──────────────────────────────────────────────────────────────────
// Key is an opaque integer whose value matches Qt::Key numeric values. Qt names do
// not appear in the ABI surface, but the values are intentionally Qt-compatible so
// the Qt window layer can translate key events without maintaining a parallel code
// system.
using Key = uint32_t;

// These constants mirror the Qt::Key enum values used by QKeyEvent::key().
namespace Keys
{
// ── Letters ───────────────────────────────────────────────────────────────
inline constexpr Key A = 'A', B = 'B', C = 'C', D = 'D', E = 'E', F = 'F', G = 'G', H = 'H', I = 'I', J = 'J', K = 'K',
                     L = 'L', M = 'M', N = 'N', O = 'O', P = 'P', Q = 'Q', R = 'R', S = 'S', T = 'T', U = 'U', V = 'V',
                     W = 'W', X = 'X', Y = 'Y', Z = 'Z';

// ── Digits ─────────────────────────────────────────────────────────────────
inline constexpr Key Num0 = '0', Num1 = '1', Num2 = '2', Num3 = '3', Num4 = '4', Num5 = '5', Num6 = '6', Num7 = '7',
                     Num8 = '8', Num9 = '9';

// ── Punctuation ───────────────────────────────────────────────────────────────
inline constexpr Key Comma = ',';
inline constexpr Key Period = '.';
inline constexpr Key Slash = '/';
inline constexpr Key Backslash = '\\';
inline constexpr Key LeftBracket = '[';
inline constexpr Key RightBracket = ']';
inline constexpr Key Minus = '-';
inline constexpr Key Equals = '=';
inline constexpr Key Apostrophe = '\'';
inline constexpr Key Backtick = '`';
inline constexpr Key Semicolon = ';';

// ── Special keys ──────────────────────────────────────────────────────────
inline constexpr Key Space = 0x20u;
inline constexpr Key Escape = 0x01000000u;
inline constexpr Key Tab = 0x01000001u;
inline constexpr Key Backspace = 0x01000003u;
inline constexpr Key Return = 0x01000004u;
inline constexpr Key Enter = 0x01000005u;
inline constexpr Key Insert = 0x01000006u;
inline constexpr Key Delete = 0x01000007u;

// ── Navigation ────────────────────────────────────────────────────────────
inline constexpr Key Home = 0x01000010u;
inline constexpr Key End = 0x01000011u;
inline constexpr Key Left = 0x01000012u;
inline constexpr Key Up = 0x01000013u;
inline constexpr Key Right = 0x01000014u;
inline constexpr Key Down = 0x01000015u;
inline constexpr Key PageUp = 0x01000016u;
inline constexpr Key PageDown = 0x01000017u;

// ── Function keys ─────────────────────────────────────────────────────────
inline constexpr Key F1 = 0x01000030u, F2 = 0x01000031u, F3 = 0x01000032u, F4 = 0x01000033u, F5 = 0x01000034u,
                     F6 = 0x01000035u, F7 = 0x01000036u, F8 = 0x01000037u, F9 = 0x01000038u, F10 = 0x01000039u,
                     F11 = 0x0100003Au, F12 = 0x0100003Bu;

// ── Lock keys ─────────────────────────────────────────────────────────────
inline constexpr Key Pause = 0x01000008u;
inline constexpr Key PrintScreen = 0x01000009u;
inline constexpr Key CapsLock = 0x01000024u;
inline constexpr Key NumLock = 0x01000025u;
inline constexpr Key ScrollLock = 0x01000026u;

// ── Extended function keys (F13–F24) ──────────────────────────────────────
inline constexpr Key F13 = 0x0100003Cu, F14 = 0x0100003Du, F15 = 0x0100003Eu, F16 = 0x0100003Fu, F17 = 0x01000040u,
                     F18 = 0x01000041u, F19 = 0x01000042u, F20 = 0x01000043u, F21 = 0x01000044u, F22 = 0x01000045u,
                     F23 = 0x01000046u, F24 = 0x01000047u;

// ── Numpad ────────────────────────────────────────────────────────────────
inline constexpr Key KeypadDivide = '/';
inline constexpr Key KeypadMultiply = '*';
inline constexpr Key KeypadMinus = '-';
inline constexpr Key KeypadPlus = '+';
inline constexpr Key KeypadEnter = Enter;
inline constexpr Key Keypad1 = Num1, Keypad2 = Num2, Keypad3 = Num3, Keypad4 = Num4, Keypad5 = Num5, Keypad6 = Num6,
                     Keypad7 = Num7, Keypad8 = Num8, Keypad9 = Num9, Keypad0 = Num0;
inline constexpr Key KeypadPeriod = '.';
inline constexpr Key KeypadEquals = '=';

// ── Application / context menu ────────────────────────────────────────────
inline constexpr Key Application = 0x01000055u;

// ── Editing helpers ───────────────────────────────────────────────────────
inline constexpr Key Help = 0x01000058u;
inline constexpr Key Menu = 0x01000055u;
inline constexpr Key Select = 0x01010000u;
inline constexpr Key Stop = 0x01000063u;
inline constexpr Key Again = 0x01000124u;
inline constexpr Key Undo = 0x01000123u;
inline constexpr Key Cut = 0x010000D0u;
inline constexpr Key Copy = 0x010000CFu;
inline constexpr Key Paste = 0x010000E2u;
inline constexpr Key Find = 0x01000122u;

// ── Volume ────────────────────────────────────────────────────────────────
inline constexpr Key VolumeUp = 0x01000072u;
inline constexpr Key VolumeDown = 0x01000070u;
inline constexpr Key Mute = 0x01000071u;

// ── Media transport ───────────────────────────────────────────────────────
inline constexpr Key MediaPlay = 0x01000080u;
inline constexpr Key MediaStop = 0x01000081u;
inline constexpr Key MediaPrev = 0x01000082u;
inline constexpr Key MediaNext = 0x01000083u;
inline constexpr Key MediaRecord = 0x01000084u;
inline constexpr Key MediaPause = 0x01000085u;
inline constexpr Key MediaPlayPause = 0x01000086u;
inline constexpr Key MediaEject = 0x010000B9u;
inline constexpr Key MediaFastForward = 0x01000102u;
inline constexpr Key MediaRewind = 0x010000C5u;
inline constexpr Key MediaSelect = 0x010000A1u;

// ── Application control ───────────────────────────────────────────────────
inline constexpr Key AcNew = 0x01000120u;
inline constexpr Key AcOpen = 0x01000121u;
inline constexpr Key AcClose = 0x010000CEu;
inline constexpr Key AcExit = 0x010000D9u;
inline constexpr Key AcSave = 0x010000EAu;
inline constexpr Key AcPrint = 0x01020002u;
inline constexpr Key AcProperties = 0x010000E1u;
inline constexpr Key AcSearch = 0x01000092u;
inline constexpr Key AcHome = 0x01000090u;
inline constexpr Key AcBack = 0x01000061u;
inline constexpr Key AcForward = 0x01000062u;
inline constexpr Key AcStop = 0x01000063u;
inline constexpr Key AcRefresh = 0x01000064u;
inline constexpr Key AcBookmarks = 0x01000091u;

inline constexpr Key Unknown = 0x01FFFFFFu;
} // namespace Keys

// Bitfield modifier flags (OR-able)
enum class Mod : uint32_t
{
    None = 0,
    Ctrl = 1u << 0,
    Shift = 1u << 1,
    Alt = 1u << 2,
};

[[nodiscard]] inline Mod operator|(Mod a, Mod b)
{
    return static_cast<Mod>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

[[nodiscard]] inline Mod operator&(Mod a, Mod b)
{
    return static_cast<Mod>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

// Returns true if flag is set in the modifier state (e.g. ModSet(e.mods, Mod::Ctrl)).
[[nodiscard]] inline bool ModSet(const Mod state, const Mod flag)
{
    return (state & flag) != Mod::None;
}

// ── File filters ──────────────────────────────────────────────────────────────

struct FileFilter
{
    const char* name;    // e.g. "Video files"
    const char* pattern; // e.g. "mp4;mkv;avi"
};

// ── Application events ────────────────────────────────────────────────────────

enum class AppEventType : std::uint8_t
{
    None,            // no event / timeout
    Quit,            // application quit requested
    WindowExposed,   // window content needs repaint
    KeyDown,         // keyboard key pressed
    KeyUp,           // keyboard key released
    RenderUpdate,    // player has a new video frame ready
    PlayerWakeup,    // player has events to drain
    MouseButtonDown, // any mouse button pressed
    MouseMotion,     // mouse cursor moved
    DropFile,        // a file was dragged and dropped onto the window
    MouseWheel,      // mouse wheel scrolled
    Custom,          // any other platform event
};

// An application-level event translated from a raw platform event.
//
// The `type` field identifies which union member is active:
//
//   type                  active member
//   ──────────────────────────────────────────────────────────────
//   None                  — (no valid member)
//   Quit                  — (no valid member)
//   WindowExposed         — (no valid member)
//   RenderUpdate          — (no valid member)
//   PlayerWakeup          — (no valid member)
//   MouseButtonDown       — (no valid member)
//   MouseMotion           — (no valid member)
//   MouseWheel            — (no valid member)
//   KeyDown               key
//   KeyUp                 key
//   DropFile              file
//   Custom                custom
//
// All payload types are POD — no heap allocation, safe across DLL boundaries.
struct AppEvent
{
    AppEventType type = AppEventType::None;

    // Reserved bytes kept for binary layout stability of the public AppEvent payload.
    // The Qt/QML event path does not expose raw native events through this storage.
    alignas(8) uint8_t reservedStorage[128]{};

    // Payload for KeyDown / KeyUp events.
    struct KeyPayload
    {
        Key key = Keys::Unknown;
        Mod mods = Mod::None;
        // True when this KeyDown was synthesised by the OS auto-repeat of a held key
        // rather than a fresh physical press. Consumers that want once-per-press
        // semantics (toggles) ignore repeats; consumers that want held-key repetition
        // (seek, list navigation) act on them.
        bool repeat = false;
    };

    // Payload for DropFile events.
    // filePath is a host-owned pointer valid only for the duration of the OnEvent() call.
    // nullptr when the platform provided no path (malformed drop).
    struct FilePayload
    {
        const char* filePath = nullptr;
    };

    // Payload for Custom events. eventType is a FrameLift-owned id returned by
    // IEventPump::RegisterCustomEventType(); userData1 is optional subsystem-owned
    // payload data whose lifetime contract is defined by the sender/receiver pair.
    struct CustomPayload
    {
        uint32_t eventType = 0;
        void* userData1 = nullptr;
    };

    union {
        KeyPayload key{};
        FilePayload file;
        CustomPayload custom;
    };

    [[nodiscard]] const KeyPayload& AsKey() const noexcept
    {
        return key;
    }

    [[nodiscard]] KeyPayload& AsKey() noexcept
    {
        return key;
    }

    [[nodiscard]] const FilePayload& AsFile() const noexcept
    {
        return file;
    }

    [[nodiscard]] FilePayload& AsFile() noexcept
    {
        return file;
    }

    [[nodiscard]] const CustomPayload& AsCustom() const noexcept
    {
        return custom;
    }

    [[nodiscard]] CustomPayload& AsCustom() noexcept
    {
        return custom;
    }
};
