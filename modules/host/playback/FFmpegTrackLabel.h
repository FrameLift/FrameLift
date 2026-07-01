#pragma once

#include <cstdint>
#include <cstdio>

// "und" is ISO 639-2's explicit "undetermined" code; it carries no useful
// information for a user, so treat it the same as a missing language tag and
// let the label fall through to the title/ordinal.
inline bool IsMeaningfulLang(const char* lang)
{
    if (!lang || lang[0] == '\0')
    {
        return false;
    }
    const bool isUnd = (lang[0] == 'u' || lang[0] == 'U') && (lang[1] == 'n' || lang[1] == 'N') &&
                       (lang[2] == 'd' || lang[2] == 'D') && lang[3] == '\0';
    return !isUnd;
}

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
    const bool hasLang = IsMeaningfulLang(lang);
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
