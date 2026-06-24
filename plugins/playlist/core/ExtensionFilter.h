#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <ranges>
#include <string>

// True if the path's extension is present in a semicolon-separated, lower-case
// extension list (e.g. "mp4;mkv;webm"). Matching is case-insensitive and the
// path's leading dot is ignored. Platform-independent — unit-tested natively.
inline bool ExtensionInList(const std::filesystem::path& p, const std::string& extensions)
{
    auto ext = p.extension().string();
    std::ranges::transform(
        ext, ext.begin(),
        [](const unsigned char c)
        {
            return std::tolower(c);
        }
    );
    if (ext.size() > 1)
    {
        ext = ext.substr(1); // drop the leading '.'
    }

    std::size_t start = 0;
    while (start < extensions.size())
    {
        const auto end = extensions.find(';', start);
        const auto len = (end == std::string::npos ? extensions.size() : end) - start;
        if (extensions.compare(start, len, ext) == 0)
        {
            return true;
        }
        if (end == std::string::npos)
        {
            break;
        }
        start = end + 1;
    }
    return false;
}
