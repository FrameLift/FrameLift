#pragma once
#include <filesystem>
#include <fstream>
#include <framelift/IModuleSettings.h>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// Concrete IModuleSettings backed by one named section in a shared INI file.
// Host-internal — never exposed to plugins directly; plugins receive IModuleSettings&.
class ModuleSettingsImpl final : public IModuleSettings
{
public:
    ModuleSettingsImpl(std::string section, std::string iniPath)
        : section_(std::move(section)), iniPath_(std::move(iniPath))
    {
        Load();
    }

    [[nodiscard]] const char* GetString(const char* key, const char* def = "") const noexcept override
    {
        const auto it = values_.find(key);
        return it != values_.end() ? it->second.c_str() : def;
    }

    [[nodiscard]] int GetInt(const char* key, int def = 0) const noexcept override
    {
        const auto it = values_.find(key);
        if (it == values_.end())
        {
            return def;
        }
        try
        {
            return std::stoi(it->second);
        }
        catch (...)
        {
            return def;
        }
    }

    [[nodiscard]] float GetFloat(const char* key, float def = 0.f) const noexcept override
    {
        const auto it = values_.find(key);
        if (it == values_.end())
        {
            return def;
        }
        try
        {
            return std::stof(it->second);
        }
        catch (...)
        {
            return def;
        }
    }

    [[nodiscard]] bool GetBool(const char* key, bool def = false) const noexcept override
    {
        const auto it = values_.find(key);
        if (it == values_.end())
        {
            return def;
        }
        return it->second == "1";
    }

    void SetString(const char* key, const char* value) noexcept override
    {
        values_[key] = value ? value : "";
    }

    void SetInt(const char* key, int value) noexcept override
    {
        values_[key] = std::to_string(value);
    }

    void SetFloat(const char* key, float value) noexcept override
    {
        std::ostringstream ss;
        ss << value;
        values_[key] = ss.str();
    }

    void SetBool(const char* key, bool value) noexcept override
    {
        values_[key] = value ? "1" : "0";
    }

    void Save() noexcept override
    {
        if (values_.empty())
        {
            return;
        }

        std::vector<std::string> lines;
        {
            std::ifstream f(iniPath_);
            std::string line;
            while (std::getline(f, line))
            {
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }
                lines.push_back(std::move(line));
            }
        }

        const std::string header = "[" + section_ + "]";
        int sectionStart = -1;
        int sectionEnd = static_cast<int>(lines.size());

        for (int i = 0; i < static_cast<int>(lines.size()); ++i)
        {
            if (lines[i] == header)
            {
                sectionStart = i;
                for (int j = i + 1; j < static_cast<int>(lines.size()); ++j)
                {
                    if (!lines[j].empty() && lines[j].front() == '[')
                    {
                        sectionEnd = j;
                        break;
                    }
                }
                break;
            }
        }

        std::vector<std::string> block;
        block.push_back(header);
        for (const auto& [k, v] : values_)
        {
            block.push_back(k + "=" + v);
        }

        if (sectionStart >= 0)
        {
            lines.erase(lines.begin() + sectionStart, lines.begin() + sectionEnd);
            lines.insert(lines.begin() + sectionStart, block.begin(), block.end());
            const int afterBlock = sectionStart + static_cast<int>(block.size());
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
            lines.insert(lines.end(), block.begin(), block.end());
        }

        // Write to a sibling temp file then rename over the target, so a crash
        // mid-write can't corrupt the shared INI.
        std::filesystem::path tmp(iniPath_);
        tmp += ".tmp";
        {
            std::ofstream f(tmp, std::ios::trunc);
            if (!f)
            {
                return;
            }
            for (const auto& l : lines)
            {
                f << l << '\n';
            }
            f.flush();
            if (!f)
            {
                return;
            }
        }
        std::error_code ec;
        std::filesystem::rename(tmp, iniPath_, ec);
        if (ec)
        {
            std::filesystem::remove(tmp, ec);
        }
    }

    [[nodiscard]] bool WasLoaded() const noexcept override
    {
        return sectionFound_;
    }

    [[nodiscard]] int KeyCount() const noexcept override
    {
        return static_cast<int>(values_.size());
    }

private:
    void Load()
    {
        std::ifstream f(iniPath_);
        if (!f)
        {
            return;
        }

        bool inSection = false;
        std::string line;
        while (std::getline(f, line))
        {
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            if (!line.empty() && line.front() == '[')
            {
                const std::string hdr = line.substr(1, line.size() - 2);
                inSection = (hdr == section_);
                if (inSection)
                {
                    sectionFound_ = true;
                }
                continue;
            }
            if (!inSection)
            {
                continue;
            }
            const auto eq = line.find('=');
            if (eq == std::string::npos)
            {
                continue;
            }
            values_[line.substr(0, eq)] = line.substr(eq + 1);
        }
    }

    std::string section_;
    std::string iniPath_;
    std::unordered_map<std::string, std::string> values_;
    bool sectionFound_ = false;
};
