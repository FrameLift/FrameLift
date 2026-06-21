#include "FontScan.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace fs = std::filesystem;

namespace
{
std::string ToLower(std::string s)
{
    std::ranges::transform(
        s, s.begin(),
        [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        }
    );
    return s;
}

bool IsFontFile(const fs::path& p)
{
    const std::string ext = ToLower(p.extension().string());
    return ext == ".ttf" || ext == ".otf";
}
} // namespace

std::string FontDisplayName(const fs::path& file)
{
    std::string name = file.stem().string();
    for (char& c : name)
    {
        if (c == '-' || c == '_')
        {
            c = ' ';
        }
    }
    return name;
}

std::vector<FontEntry> ScanFontDirs(const std::vector<fs::path>& dirs)
{
    std::vector<FontEntry> entries;

    for (const auto& dir : dirs)
    {
        std::error_code ec;
        if (!fs::exists(dir, ec) || ec)
        {
            continue;
        }

        fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
        if (ec)
        {
            continue;
        }
        const fs::recursive_directory_iterator end;
        for (; it != end; it.increment(ec))
        {
            if (ec)
            {
                break;
            }
            const fs::path& p = it->path();
            std::error_code fec;
            if (!fs::is_regular_file(p, fec) || fec)
            {
                continue;
            }
            if (!IsFontFile(p))
            {
                continue;
            }
            entries.push_back({FontDisplayName(p), p.string()});
        }
    }

    // Sort case-insensitively by display name.
    std::sort(
        entries.begin(), entries.end(),
        [](const FontEntry& a, const FontEntry& b)
        {
            return ToLower(a.name) < ToLower(b.name);
        }
    );

    // Dedup by (case-insensitive) name — first occurrence wins.
    entries.erase(
        std::ranges::unique(
            entries,
            [](const FontEntry& a, const FontEntry& b)
            {
                return ToLower(a.name) == ToLower(b.name);
            }
        ).begin(),
        entries.end()
    );

    return entries;
}

std::vector<fs::path> SystemFontDirs()
{
    std::vector<fs::path> dirs;

#ifdef _WIN32
    if (const char* windir = std::getenv("WINDIR"); windir && windir[0])
    {
        dirs.emplace_back(fs::path(windir) / "Fonts");
    }
    else
    {
        dirs.emplace_back("C:\\Windows\\Fonts");
    }
    if (const char* localApp = std::getenv("LOCALAPPDATA"); localApp && localApp[0])
    {
        dirs.emplace_back(fs::path(localApp) / "Microsoft" / "Windows" / "Fonts");
    }
#else
    dirs.emplace_back("/usr/share/fonts");
    dirs.emplace_back("/usr/local/share/fonts");
    if (const char* home = std::getenv("HOME"); home && home[0])
    {
        dirs.emplace_back(fs::path(home) / ".local" / "share" / "fonts");
    }
#endif

    return dirs;
}