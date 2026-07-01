#pragma once
#include <QSettings>
#include <QString>
#include <QStringList>
#include <framelift/IModuleSettings.h>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>

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

        // QSettings loads the shared INI, overwrites only this section's keys, and
        // re-emits every other section untouched (host settings, sibling plugins) —
        // and syncs atomically, so no manual temp-file-rename dance is needed.
        QSettings qs(QString::fromStdString(iniPath_), QSettings::IniFormat);
        qs.beginGroup(QString::fromStdString(section_));
        for (const auto& [k, v] : values_)
        {
            qs.setValue(QString::fromStdString(k), QString::fromStdString(v));
        }
        qs.endGroup();
        qs.sync();
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
        QSettings qs(QString::fromStdString(iniPath_), QSettings::IniFormat);
        const QString group = QString::fromStdString(section_);
        sectionFound_ = qs.childGroups().contains(group);

        qs.beginGroup(group);
        for (const QString& key : qs.childKeys())
        {
            values_[key.toStdString()] = qs.value(key).toString().toStdString();
        }
        qs.endGroup();
    }

    std::string section_;
    std::string iniPath_;
    std::unordered_map<std::string, std::string> values_;
    bool sectionFound_ = false;
};
