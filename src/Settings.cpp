#include "Settings.h"

#include "platform/ffmpeg/VideoDecodeMode.h"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// ── Field registration table ──────────────────────────────────────────────────
// Each entry: { ini-key, loader, saver }
// To add a new setting: add ONE MakeField row — nothing else to change.
namespace
{
struct Field
{
    const char* key;
    const char* desc;
    std::function<void(Settings&, const std::string&)> load;
    std::function<std::string(const Settings&)> save;
};

// ── Type-dispatched factory overloads ─────────────────────────────────────
Field MakeField(const char* key, const char* desc, float Settings::* m)
{
    return {
        key,
        desc,
        [m](Settings& s, const std::string& v)
        {
            s.*m = std::stof(v);
        },
        [m](const Settings& s)
        {
            return std::to_string(s.*m);
        }
    };
}

Field MakeField(const char* key, const char* desc, int Settings::* m)
{
    return {
        key,
        desc,
        [m](Settings& s, const std::string& v)
        {
            s.*m = std::stoi(v);
        },
        [m](const Settings& s)
        {
            return std::to_string(s.*m);
        }
    };
}

Field MakeField(const char* key, const char* desc, bool Settings::* m)
{
    return {
        key,
        desc,
        [m](Settings& s, const std::string& v)
        {
            s.*m = (v == "1");
        },
        [key, m](const Settings& s)
        {
            if (std::string_view(key) == "playback.hwdec")
            {
                const VideoDecodeMode mode = s.hwdec ? VideoDecodeModeFromString(s.hwdecMode) : VideoDecodeMode::Off;
                return std::string(IsVideoDecodeModeEnabled(mode) ? "1" : "0");
            }
            return std::string(s.*m ? "1" : "0");
        }
    };
}

Field MakeField(const char* key, const char* desc, std::string Settings::* m)
{
    return {
        key,
        desc,
        [m](Settings& s, const std::string& v)
        {
            s.*m = v;
        },
        [key, m](const Settings& s)
        {
            if (std::string_view(key) == "playback.hwdecMode")
            {
                const VideoDecodeMode mode = s.hwdec ? VideoDecodeModeFromString(s.*m) : VideoDecodeMode::Off;
                return std::string(VideoDecodeModeName(mode));
            }
            return s.*m;
        }
    };
}

// ── Registration table ────────────────────────────────────────────────────
// Keys are generated automatically from SETTINGS_FIELDS as "section.name".
// To add a setting: add a row to the macro in Settings.h — nothing here changes.
const std::vector<Field>& Fields()
{
    static const std::vector<Field> table = {
#define X(section, name, type, def, desc) MakeField(#section "." #name, desc, &Settings::name),
        SETTINGS_FIELDS(X)
#undef X
    };
    return table;
}

// Built once per process; used by Load() for O(1) key lookup.
const std::unordered_map<std::string, const Field*>& FieldMap()
{
    static const auto map = []
    {
        std::unordered_map<std::string, const Field*> m;
        for (const auto& f : Fields())
        {
            m.emplace(f.key, &f);
        }
        return m;
    }();
    return map;
}
} // namespace

// ── Settings::Load ────────────────────────────────────────────────────────────

static std::vector<std::string> SplitSemicolon(const std::string& s)
{
    std::vector<std::string> parts;
    std::string::size_type start = 0;
    while (true)
    {
        const auto sep = s.find(';', start);
        const auto token = s.substr(start, sep == std::string::npos ? std::string::npos : sep - start);
        if (!token.empty())
        {
            parts.push_back(token);
        }
        if (sep == std::string::npos)
        {
            break;
        }
        start = sep + 1;
    }
    return parts;
}

void Settings::Load(const std::string& path)
{
    std::ifstream file(path);
    if (!file)
    {
        Save(path);
        return;
    }

    // An existing but empty file (e.g. a truncated/blank settings.ini) is treated
    // like a missing one: seed it with the current defaults so it is never left
    // blank. Without this, defaults stay in memory only and the file stays empty.
    if (file.peek() == std::ifstream::traits_type::eof())
    {
        file.close();
        Save(path);
        return;
    }

    const auto& map = FieldMap();
    std::string section;
    std::string line;
    bool sawPlaybackHwdecMode = false;
    bool sawPlaybackHwdec = false;
    while (std::getline(file, line))
    {
        if (line.empty() || line.front() == '#')
        {
            continue;
        }

        if (line.front() == '[')
        {
            const auto close = line.find(']');
            if (close != std::string::npos)
            {
                section = line.substr(1, close - 1);
            }
            continue;
        }

        const auto eq = line.find('=');
        if (eq == std::string::npos)
        {
            continue;
        }

        const std::string key = section + '.' + line.substr(0, eq);
        const std::string val = line.substr(eq + 1);

        if (key == "playback.hwdecMode")
        {
            sawPlaybackHwdecMode = true;
        }
        else if (key == "playback.hwdec")
        {
            sawPlaybackHwdec = true;
        }

        // Handle enabledPlugins separately — not in the X-macro field table.
        if (key == "plugins.enabled")
        {
            enabledPlugins = SplitSemicolon(val);
            continue;
        }

        const auto it = map.find(key);
        if (it == map.end())
        {
            continue;
        }

        try
        {
            it->second->load(*this, val);
        }
        catch (...)
        {
        }
    }

    if (sawPlaybackHwdecMode)
    {
        hwdecMode = VideoDecodeModeName(VideoDecodeModeFromString(hwdecMode));
        hwdec = IsVideoDecodeModeEnabled(VideoDecodeModeFromString(hwdecMode));
    }
    else if (sawPlaybackHwdec && !hwdec)
    {
        hwdecMode = VideoDecodeModeName(VideoDecodeMode::Off);
    }
    else
    {
        hwdecMode = VideoDecodeModeName(VideoDecodeMode::Auto);
        hwdec = true;
    }
}

// ── Settings::Save ────────────────────────────────────────────────────────────
// Synchronous section-aware merge-save: owned sections are replaced in place;
// unknown sections (e.g. plugin data) are preserved unchanged. The merge keeps
// keys written by PluginSettings::Save() intact.
void Settings::Save(const std::string& path) const
{
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);

    // Build owned sections in field order, preserving first-seen section sequence.
    // descForKey maps a full "section.name" key to its "# ..." documentation comment.
    std::vector<std::string> sectionOrder;
    std::unordered_map<std::string, std::vector<std::string>> ownedLines;
    std::unordered_map<std::string, std::string> descForKey;

    for (const auto& field : Fields())
    {
        const std::string key(field.key);
        const auto dot = key.find('.');
        const std::string section = key.substr(0, dot);
        const std::string name = key.substr(dot + 1);
        if (!ownedLines.contains(section))
        {
            sectionOrder.push_back(section);
        }
        ownedLines[section].push_back(name + "=" + field.save(*this));
        if (field.desc && field.desc[0] != '\0')
        {
            descForKey[key] = field.desc;
        }
    }

    // [plugins] section
    sectionOrder.push_back("plugins");
    descForKey["plugins.enabled"] = "Plugin DLLs to load, in order (semicolon-separated).";
    {
        std::string line = "enabled=";
        for (std::size_t i = 0; i < enabledPlugins.size(); ++i)
        {
            if (i > 0)
            {
                line += ';';
            }
            line += enabledPlugins[i];
        }
        ownedLines["plugins"].push_back(std::move(line));
    }

    // Emit an owned key line into `block`, prefixed by its "# ..." comment when one exists.
    const auto emitKey = [&](std::vector<std::string>& block, const std::string& section, const std::string& name,
                             const std::string& valueLine)
    {
        const auto it = descForKey.find(section + "." + name);
        if (it != descForKey.end())
        {
            block.push_back("# " + it->second);
        }
        block.push_back(valueLine);
    };

    // Read existing file into lines so we can preserve unknown sections.
    std::vector<std::string> lines;
    {
        std::ifstream in(path);
        std::string line;
        while (std::getline(in, line))
        {
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            lines.push_back(std::move(line));
        }
    }

    // Helper: find [sectionName] header position and end of its content.
    const auto findSection = [&](const std::string& name) -> std::pair<int, int>
    {
        const std::string header = "[" + name + "]";
        for (int i = 0; i < static_cast<int>(lines.size()); ++i)
        {
            if (lines[i] == header)
            {
                int end = static_cast<int>(lines.size());
                for (int j = i + 1; j < static_cast<int>(lines.size()); ++j)
                {
                    if (!lines[j].empty() && lines[j].front() == '[')
                    {
                        end = j;
                        break;
                    }
                }
                return {i, end};
            }
        }
        return {-1, -1};
    };

    // Update each owned section in-place (key-aware: preserves unknown keys such as
    // plugin keybinds), or append if absent.
    for (const auto& section : sectionOrder)
    {
        const auto& newLines = ownedLines.at(section);
        const auto [start, end] = findSection(section);

        if (start >= 0)
        {
            // Build key→value map of our owned keys.
            std::unordered_map<std::string, std::string> newKV;
            for (const auto& l : newLines)
            {
                const auto eq = l.find('=');
                if (eq != std::string::npos)
                {
                    newKV[l.substr(0, eq)] = l.substr(eq + 1);
                }
            }

            // Rebuild section: update owned keys, preserve unknown keys (e.g. plugin keybinds).
            std::vector<std::string> updatedBlock;
            updatedBlock.push_back("[" + section + "]");
            std::unordered_set<std::string> written;

            for (int i = start + 1; i < end; ++i)
            {
                const auto& existingLine = lines[i];
                if (existingLine.empty())
                {
                    continue;
                }
                // Drop auto-generated comments; they are regenerated per owned key below.
                // This keeps repeated saves idempotent (no comment duplication).
                if (existingLine.front() == '#')
                {
                    continue;
                }
                const auto eq = existingLine.find('=');
                if (eq == std::string::npos)
                {
                    updatedBlock.push_back(existingLine);
                    continue;
                }
                const std::string key = existingLine.substr(0, eq);
                const auto it = newKV.find(key);
                if (it != newKV.end())
                {
                    emitKey(updatedBlock, section, key, key + "=" + it->second);
                    written.insert(key);
                }
                else
                {
                    updatedBlock.push_back(existingLine);
                }
            }

            // Append owned keys not yet in the section (new fields added since last save).
            for (const auto& l : newLines)
            {
                const auto eq = l.find('=');
                const std::string key = (eq != std::string::npos) ? l.substr(0, eq) : l;
                if (!written.contains(key))
                {
                    emitKey(updatedBlock, section, key, l);
                }
            }

            // Erase existing section (strip trailing blanks before next header).
            int trimEnd = end;
            while (trimEnd > start + 1 && lines[trimEnd - 1].empty())
            {
                --trimEnd;
            }
            lines.erase(lines.begin() + start, lines.begin() + trimEnd);
            lines.insert(lines.begin() + start, updatedBlock.begin(), updatedBlock.end());

            // Ensure blank separator follows before the next section.
            const int afterBlock = start + static_cast<int>(updatedBlock.size());
            if (afterBlock < static_cast<int>(lines.size()) && !lines[afterBlock].empty())
            {
                lines.insert(lines.begin() + afterBlock, "");
            }
        }
        else
        {
            if (!lines.empty())
            {
                lines.emplace_back();
            }
            lines.push_back("[" + section + "]");
            for (const auto& l : newLines)
            {
                const auto eq = l.find('=');
                const std::string key = (eq != std::string::npos) ? l.substr(0, eq) : l;
                emitKey(lines, section, key, l);
            }
        }
    }

    std::ofstream out(path, std::ios::trunc);
    for (const auto& l : lines)
    {
        out << l << '\n';
    }
}
