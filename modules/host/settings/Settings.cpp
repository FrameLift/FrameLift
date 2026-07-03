#include "Settings.h"

#include "SettingsRegistry.h"

// Settings.cpp is the single place that knows the full set of settings sections.
#include "CoreSettings.h"     // host-core: General/Files/Keybinds
#include "AudioSettings.h"    // host/audio
#include "PlaybackSettings.h" // media/ffmpeg
#include "SubtitleSettings.h" // media/ffmpeg
#include "CacheSettings.h"    // host/read-ahead
#include "ThemeSettings.h"    // host/ui
#include "UISettings.h"       // host/ui

#include <QSettings>
#include <QString>
#include <QStringList>

#include <cstdlib>
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

// ── Section registration ──────────────────────────────────────────────────────
Settings::Settings()
{
    Add<PlaybackSettings>();
    Add<SubtitleSettings>();
    Add<CacheSettings>();
    Add<UISettings>();
    Add<FilesSettings>();
    Add<AudioSettings>();
    Add<ThemeSettings>();
    Add<KeybindSettings>();
}

void Settings::ResetToDefaults()
{
    // Assign through the section references so the stored objects keep their
    // addresses (a SettingsRegistry bound to this instance stays valid).
    Get<PlaybackSettings>() = {};
    Get<SubtitleSettings>() = {};
    Get<CacheSettings>() = {};
    Get<UISettings>() = {};
    Get<FilesSettings>() = {};
    Get<AudioSettings>() = {};
    Get<ThemeSettings>() = {};
    Get<KeybindSettings>() = {};
}

// ── Registry assembly ─────────────────────────────────────────────────────────
// Each module registers the fields it owns. Section order here fixes the order of
// sections written to settings.ini.
SettingsRegistry BuildSettingsRegistry(Settings& s)
{
    SettingsRegistry reg;
    RegisterPlaybackSettings(reg, s.Get<PlaybackSettings>());
    RegisterSubtitleSettings(reg, s.Get<SubtitleSettings>());
    RegisterCacheSettings(reg, s.Get<CacheSettings>());
    RegisterUISettings(reg, s.Get<UISettings>());
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
    const SettingsRegistry reg = BuildSettingsRegistry(*this);

    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);

    QSettings qs(QString::fromStdString(path), QSettings::IniFormat);
    for (const auto& field : reg.Fields())
    {
        qs.setValue(ToQtKey(field.key), QString::fromStdString(field.save()));
    }
    qs.sync();
}

void Settings::ApplyLaunchEnvironmentOverrides()
{
    if (const char* modeEnv = std::getenv("FL_ACCEL_MODE"); modeEnv && modeEnv[0])
    {
        PlaybackSettings& playback = Get<PlaybackSettings>();
        const VideoDecodeMode mode = VideoDecodeModeFromString(modeEnv);
        playback.hwdecMode = VideoDecodeModeName(mode);
        playback.hwdec = IsVideoDecodeModeEnabled(mode);
    }
}
