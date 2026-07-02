#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

// A fuzzy-matched sidecar file discovered next to the media (Phase 5 auto-load).
struct ExternalSource
{
    std::string path;
    bool isAudio = false; // else subtitle
};

// Scan the media file's directory for fuzzy-matching sidecar subtitle/audio files:
// a sidecar's filename must contain the media stem (case-insensitive) and carry a
// known subtitle/audio extension. Pure std::filesystem (no libav) → unit-tested in
// the native suite. Returns an empty list when both auto-load gates are off or the
// path has no usable directory/stem.
inline std::vector<ExternalSource> ScanSidecarFiles(const std::string& mediaPath, bool subAutoLoad,
                                                    bool audioFileAutoLoad)
{
    std::vector<ExternalSource> found;
    if (!subAutoLoad && !audioFileAutoLoad)
    {
        return found;
    }

    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path media(mediaPath);
    const fs::path dir = media.parent_path();
    const std::string stem = media.stem().string();
    if (dir.empty() || stem.empty())
    {
        return found;
    }

    const auto lower = [](std::string s)
    {
        for (char& c : s)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return s;
    };
    const std::string stemL = lower(stem);
    const std::string mediaName = media.filename().string();

    static constexpr std::array<const char*, 4> kSubExt = {".srt", ".ass", ".ssa", ".sub"};
    static constexpr std::array<const char*, 6> kAudExt = {".mka", ".m4a", ".aac", ".ac3", ".dts", ".flac"};

    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
    {
        if (!it->is_regular_file(ec))
        {
            continue;
        }
        const fs::path p = it->path();
        if (p.filename().string() == mediaName)
        {
            continue; // skip the media file itself
        }
        const std::string nameL = lower(p.filename().string());
        if (nameL.find(stemL) == std::string::npos)
        {
            continue; // fuzzy match: sidecar name must contain the media stem
        }
        const std::string ext = lower(p.extension().string());
        const auto matches = [&ext](const auto& list)
        {
            return std::find_if(
                       list.begin(), list.end(),
                       [&](const char* e)
                       {
                           return ext == e;
                       }
                   ) != list.end();
        };
        if (subAutoLoad && matches(kSubExt))
        {
            found.push_back({p.string(), false});
        }
        else if (audioFileAutoLoad && matches(kAudExt))
        {
            found.push_back({p.string(), true});
        }
    }
    return found;
}
