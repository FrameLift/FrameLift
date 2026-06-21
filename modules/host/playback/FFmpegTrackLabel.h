#pragma once

#include <cstdint>
#include <cstdio>

// Builds a human-readable audio/subtitle track label into a fixed 256-byte,
// NUL-terminated buffer (issue #8, Phase 5). Precedence is title → language →
// "Track N", where N is the 1-based ordinal among tracks of the same kind. For an external sidecar file
// the source filename is used; when a title/language is also present it is appended
// in parentheses so sidecars are distinguishable. Pure (no libav) → unit-tested in
// the native suite. Any argument may be null or empty.
inline void MakeTrackLabel(char out[256], const char* title, const char* lang, int64_t ordinal,
                           const char* externalBasename)
{
    const bool hasTitle = title && title[0] != '\0';
    const bool hasLang = lang && lang[0] != '\0';
    const bool hasFile = externalBasename && externalBasename[0] != '\0';

    if (hasTitle && hasFile)
    {
        std::snprintf(out, 256, "%s (%s)", title, externalBasename);
    }
    else if (hasTitle)
    {
        std::snprintf(out, 256, "%s", title);
    }
    else if (hasLang && hasFile)
    {
        std::snprintf(out, 256, "%s (%s)", lang, externalBasename);
    }
    else if (hasLang)
    {
        std::snprintf(out, 256, "%s", lang);
    }
    else if (hasFile)
    {
        std::snprintf(out, 256, "%s", externalBasename);
    }
    else
    {
        std::snprintf(out, 256, "Track %lld", static_cast<long long>(ordinal));
    }
}
