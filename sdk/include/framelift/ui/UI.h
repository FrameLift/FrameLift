#pragma once

#include <cstdint>

// ── UI primitive types ────────────────────────────────────────────────────────
// Own types that mirror Dear ImGui's, with no dependency on imgui.h.
// These are the only UI-related types that should appear in non-abstraction code.

namespace UI
{
// ── Geometry ──────────────────────────────────────────────────────────────────

struct Vec2
{
    float x = 0.f, y = 0.f;
    constexpr Vec2() = default;

    constexpr Vec2(const float x, const float y) : x(x), y(y)
    {
    }
};

// ── Color ─────────────────────────────────────────────────────────────────────

// Normalized RGBA color (0–1 per channel).
struct Color4f
{
    float r = 0.f, g = 0.f, b = 0.f, a = 1.f;
    constexpr Color4f() = default;

    constexpr Color4f(const float r, const float g, const float b, const float a = 1.f) : r(r), g(g), b(b), a(a)
    {
    }
};

// Packed RGBA — same byte layout as ImGui's ImU32 / IM_COL32.
using Color32 = uint32_t;

// Build a packed Color32 from byte components (0–255).  constexpr-friendly.
constexpr Color32 MakeColor32(const int r, const int g, const int b, const int a = 255)
{
    return static_cast<uint32_t>(a & 0xFF) << 24 | static_cast<uint32_t>(b & 0xFF) << 16 |
           static_cast<uint32_t>(g & 0xFF) << 8 | static_cast<uint32_t>(r & 0xFF);
}

// ── Enumerations ──────────────────────────────────────────────────────────────

// Condition for SetNextWindowPos / SetNextWindowSize.
enum class Cond : std::uint8_t
{
    Always,
    FirstUseEver
};

// Push-able style variables.
enum class StyleVar : std::uint8_t
{
    WindowPadding,
    WindowRounding
};

// Push-able color slots.
enum class ColorSlot : std::uint8_t
{
    Text,
    WindowBg,
    Border,
    ChildBg,
    Button,
    ButtonHovered,
    ButtonActive
};

// Window creation flags (bitmask).
enum class WindowFlags : std::uint16_t
{
    None = 0,
    NoTitleBar = 1 << 0,
    NoResize = 1 << 1,
    NoMove = 1 << 2,
    NoScrollbar = 1 << 3,
    NoCollapse = 1 << 5,
    NoSavedSettings = 1 << 8,
    NoBringToFrontOnFocus = 1 << 13,
};

[[nodiscard]] constexpr WindowFlags operator|(WindowFlags a, WindowFlags b)
{
    return static_cast<WindowFlags>(static_cast<int>(a) | static_cast<int>(b));
}

// Selectable widget flags.
enum class SelectableFlags : std::uint8_t
{
    None = 0
};
} // namespace UI