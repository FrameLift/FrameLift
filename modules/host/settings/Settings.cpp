#include "Settings.h"

#include "SettingsRegistry.h"

// Settings.cpp is the single place that knows the full set of settings sections.
#include "CoreSettings.h"     // host-core: General/Files/Keybinds
#include "AudioSettings.h"    // host/audio
#include "PlaybackSettings.h" // media/ffmpeg
#include "SubtitleSettings.h" // media/ffmpeg
#include "CacheSettings.h"    // host/read-ahead
#include "GraphicsSettings.h" // gfx/graphics-core
#include "ThemeSettings.h"    // host/ui
#include "UiSettings.h"       // host/ui

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// ── Section registration ──────────────────────────────────────────────────────
Settings::Settings()
{
    Add<GeneralSettings>();
    Add<PlaybackSettings>();
    Add<SubtitleSettings>();
    Add<CacheSettings>();
    Add<GraphicsSettings>();
    Add<UiSettings>();
    Add<FilesSettings>();
    Add<AudioSettings>();
    Add<ThemeSettings>();
    Add<KeybindSettings>();
}

void Settings::ResetToDefaults()
{
    // Assign through the section references so the stored objects keep their
    // addresses (a SettingsRegistry bound to this instance stays valid).
    Get<GeneralSettings>() = {};
    Get<PlaybackSettings>() = {};
    Get<SubtitleSettings>() = {};
    Get<CacheSettings>() = {};
    Get<GraphicsSettings>() = {};
    Get<UiSettings>() = {};
    Get<FilesSettings>() = {};
    Get<AudioSettings>() = {};
    Get<ThemeSettings>() = {};
    Get<KeybindSettings>() = {};
}

// ── Registry assembly ─────────────────────────────────────────────────────────
// Each module registers the fields it owns. Section order here fixes the order of
// sections written to settings.ini, so it matches the historical layout.
SettingsRegistry BuildSettingsRegistry(Settings& s)
{
    SettingsRegistry reg;
    RegisterGeneralSettings(reg, s.Get<GeneralSettings>());
    RegisterPlaybackSettings(reg, s.Get<PlaybackSettings>());
    RegisterSubtitleSettings(reg, s.Get<SubtitleSettings>());
    RegisterCacheSettings(reg, s.Get<CacheSettings>());
    RegisterGraphicsSettings(reg, s.Get<GraphicsSettings>());
    RegisterUiSettings(reg, s.Get<UiSettings>());
    RegisterFilesSettings(reg, s.Get<FilesSettings>());
    RegisterAudioSettings(reg, s.Get<AudioSettings>());
    RegisterThemeSettings(reg, s.Get<ThemeSettings>());
    RegisterKeybindSettings(reg, s.Get<KeybindSettings>());
    return reg;
}

// ── Settings::Load ────────────────────────────────────────────────────────────
void Settings::Load(const std::string& path)
{
    const SettingsRegistry reg = BuildSettingsRegistry(*this);

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

    std::set<std::string> seen;
    std::string section;
    std::string line;
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

        seen.insert(key);

        const SettingField* f = reg.Find(key);
        if (!f)
        {
            continue;
        }
        try
        {
            f->load(val);
        }
        catch (...)
        {
        }
    }

    // Modules reconcile cross-field defaults now that every key has been parsed.
    reg.RunPostLoad(seen);
}

// ── Settings::Save ────────────────────────────────────────────────────────────
// Synchronous section-aware merge-save: owned sections are replaced in place;
// unknown sections (e.g. per-module data) are preserved unchanged. The merge keeps
// keys written by ModuleSettings::Save() intact.
void Settings::Save(const std::string& path)
{
    const SettingsRegistry reg = BuildSettingsRegistry(*this);

    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);

    // Build owned sections in field order, preserving first-seen section sequence.
    // descForKey maps a full "section.name" key to its "# ..." documentation comment.
    std::vector<std::string> sectionOrder;
    std::unordered_map<std::string, std::vector<std::string>> ownedLines;
    std::unordered_map<std::string, std::string> descForKey;

    for (const auto& field : reg.Fields())
    {
        const std::string& key = field.key;
        const auto dot = key.find('.');
        const std::string section = key.substr(0, dot);
        const std::string name = key.substr(dot + 1);
        if (!ownedLines.contains(section))
        {
            sectionOrder.push_back(section);
        }
        ownedLines[section].push_back(name + "=" + field.save());
        if (!field.desc.empty())
        {
            descForKey[key] = field.desc;
        }
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
