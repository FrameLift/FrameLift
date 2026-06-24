#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

// ── Pure theme/color helpers (no imgui, no SDL) ───────────────────────────────
// Shared by the host UI module, the SettingsMenu plugin, and unit tests.
// Header-only so it compiles into every translation unit that needs it without a
// shared library — consistent with how the SDK helper sources are compiled in.
namespace ThemeUtil
{
inline constexpr const char* PresetNames[] = {"dark", "light"};
inline constexpr int PresetCount = 2;

// Parse "#RRGGBB" or "RRGGBB" (case-insensitive) into rgb[3] in 0–1.
// Returns false on malformed input, leaving rgb untouched.
inline bool ParseHexColor(const char* s, float rgb[3])
{
    if (!s)
    {
        return false;
    }
    if (s[0] == '#')
    {
        ++s;
    }
    if (std::strlen(s) != 6)
    {
        return false;
    }

    auto nibble = [](char c, int& out) -> bool
    {
        if (c >= '0' && c <= '9')
        {
            out = c - '0';
        }
        else if (c >= 'a' && c <= 'f')
        {
            out = c - 'a' + 10;
        }
        else if (c >= 'A' && c <= 'F')
        {
            out = c - 'A' + 10;
        }
        else
        {
            return false;
        }
        return true;
    };

    int v[6];
    for (int i = 0; i < 6; ++i)
    {
        if (!nibble(s[i], v[i]))
        {
            return false;
        }
    }
    rgb[0] = static_cast<float>(v[0] * 16 + v[1]) / 255.f;
    rgb[1] = static_cast<float>(v[2] * 16 + v[3]) / 255.f;
    rgb[2] = static_cast<float>(v[4] * 16 + v[5]) / 255.f;
    return true;
}

// Format rgb[3] (0–1) into "#RRGGBB" + NUL. out must hold at least 8 chars.
inline void FormatHexColor(const float rgb[3], char out[8])
{
    auto byte = [](float f) -> int
    {
        const int v = static_cast<int>(std::lround(std::clamp(f, 0.f, 1.f) * 255.f));
        return std::clamp(v, 0, 255);
    };
    std::snprintf(out, 8, "#%02X%02X%02X", byte(rgb[0]), byte(rgb[1]), byte(rgb[2]));
}

// 0 dark / 1 light; unknown (incl. nullptr) falls back to 0.
inline int PresetIndex(const char* preset)
{
    if (preset)
    {
        for (int i = 0; i < PresetCount; ++i)
        {
            if (std::strcmp(preset, PresetNames[i]) == 0)
            {
                return i;
            }
        }
    }
    return 0;
}

// RGB <-> HSV, all channels in 0–1 (hue normalized to 0–1, not degrees).
inline void RgbToHsv(const float rgb[3], float hsv[3])
{
    const float r = rgb[0], g = rgb[1], b = rgb[2];
    const float maxc = std::max({r, g, b});
    const float minc = std::min({r, g, b});
    const float delta = maxc - minc;

    hsv[2] = maxc;                            // value
    hsv[1] = maxc > 0.f ? delta / maxc : 0.f; // saturation

    float h = 0.f;
    if (delta > 0.f)
    {
        if (maxc == r)
        {
            h = (g - b) / delta;
        }
        else if (maxc == g)
        {
            h = 2.f + (b - r) / delta;
        }
        else
        {
            h = 4.f + (r - g) / delta;
        }
        h /= 6.f;
        if (h < 0.f)
        {
            h += 1.f;
        }
    }
    hsv[0] = h;
}

inline void HsvToRgb(const float hsv[3], float rgb[3])
{
    const float h = hsv[0], s = hsv[1], v = hsv[2];
    if (s <= 0.f)
    {
        rgb[0] = rgb[1] = rgb[2] = v;
        return;
    }
    const float hh = (h - std::floor(h)) * 6.f;
    const int i = static_cast<int>(hh);
    const float f = hh - static_cast<float>(i);
    const float p = v * (1.f - s);
    const float q = v * (1.f - s * f);
    const float t = v * (1.f - s * (1.f - f));
    switch (i)
    {
    case 0:
        rgb[0] = v;
        rgb[1] = t;
        rgb[2] = p;
        break;
    case 1:
        rgb[0] = q;
        rgb[1] = v;
        rgb[2] = p;
        break;
    case 2:
        rgb[0] = p;
        rgb[1] = v;
        rgb[2] = t;
        break;
    case 3:
        rgb[0] = p;
        rgb[1] = q;
        rgb[2] = v;
        break;
    case 4:
        rgb[0] = t;
        rgb[1] = p;
        rgb[2] = v;
        break;
    default:
        rgb[0] = v;
        rgb[1] = p;
        rgb[2] = q;
        break;
    }
}

// Retint an existing style color in place: adopt the accent's hue & saturation
// but keep the slot's own value (brightness) and alpha. This makes one accent
// mapping read correctly across Dark/Light/Classic, since each slot keeps the
// brightness/translucency the preset gave it.
inline void RetintColor(float rgba[4], const float accentRgb[3])
{
    float slotHsv[3];
    RgbToHsv(rgba, slotHsv);
    float accentHsv[3];
    RgbToHsv(accentRgb, accentHsv);

    const float merged[3] = {accentHsv[0], accentHsv[1], slotHsv[2]};
    float out[3];
    HsvToRgb(merged, out);
    rgba[0] = out[0];
    rgba[1] = out[1];
    rgba[2] = out[2];
    // rgba[3] (alpha) preserved.
}
} // namespace ThemeUtil
