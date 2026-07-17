#include "Settings.h"

#include <QSettings>
#include <QString>
#include <QStringList>

#include <filesystem>
#include <set>
#include <string>

namespace
{
// Registry keys are dotted ("audio.defaultLanguage"); QSettings groups keys by '/'.
// Section names never contain '.', so converting the first '.' is sufficient.
QString ToQtKey(const std::string& dotted)
{
    QString k = QString::fromStdString(dotted);
    const int dot = k.indexOf('.');
    if (dot >= 0)
    {
        k[dot] = '/';
    }
    return k;
}

std::string ToSettingString(const QVariant& value, SettingType type)
{
    if (type == SettingType::String)
    {
        const QStringList list = value.toStringList();
        if (list.size() > 1)
        {
            return list.join(';').toStdString();
        }
    }
    return value.toString().toStdString();
}
} // namespace

void Settings::ResetToDefaults()
{
    for (const SectionBinding& binding : sectionBindings_)
    {
        binding.reset(sections_.at(binding.type));
    }
}

SettingsRegistry Settings::BuildRegistry()
{
    SettingsRegistry reg;
    for (const SectionBinding& binding : sectionBindings_)
    {
        binding.bind(reg, sections_.at(binding.type));
    }
    return reg;
}

// ── Settings::Load ────────────────────────────────────────────────────────────
void Settings::Load(const std::string& path)
{
    const SettingsRegistry reg = BuildRegistry();

    // A missing or empty file (e.g. a truncated/blank settings.ini) is seeded with
    // the current defaults so it is never left blank — otherwise defaults would stay
    // in memory only and the file would remain absent/empty.
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || std::filesystem::file_size(path, ec) == 0)
    {
        Save(path);
        return;
    }

    QSettings qs(QString::fromStdString(path), QSettings::IniFormat);

    std::set<std::string> seen;
    for (const auto& field : reg.Fields())
    {
        const QString qkey = ToQtKey(field.key);
        if (!qs.contains(qkey))
        {
            continue;
        }
        seen.insert(field.key);
        try
        {
            field.load(ToSettingString(qs.value(qkey), field.type));
        }
        catch (...)
        {
        }
    }

    // Modules reconcile cross-field defaults now that every key has been parsed.
    reg.RunPostLoad(seen);
}

// ── Settings::Save ────────────────────────────────────────────────────────────
// QSettings owns the read-modify-write: it loads the existing file, overwrites only
// the owned keys, and re-emits everything else untouched — so plugin sections written
// by ModuleSettings::Save() are preserved automatically. Values are stored as quoted
// QStrings, so commas/spaces round-trip as a single string (no INI list-splitting).
void Settings::Save(const std::string& path)
{
    const SettingsRegistry reg = BuildRegistry();

    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);

    QSettings qs(QString::fromStdString(path), QSettings::IniFormat);
    for (const auto& field : reg.Fields())
    {
        qs.setValue(ToQtKey(field.key), QString::fromStdString(field.save()));
    }
    qs.sync();
}
