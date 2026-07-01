#include "SettingsService.h"
#include "PlaybackSettings.h"
#include "Settings.h"
#include "VideoDecodeMode.h"
#include <cstddef>
#include <cstring>
#include <ranges>
#include <string>
#include <utility>

SettingsService::SettingsService(Settings* settings, std::string settingsPath)
    : settingsPath_(std::move(settingsPath)), settings_(settings)
{
    // Bind the field registry to the live settings, and snapshot the serialized
    // defaults from a fresh Settings so EnumerateSettings can report them.
    settingsRegistry_ = BuildSettingsRegistry(*settings_);
    Settings defaults;
    const SettingsRegistry defaultsReg = BuildSettingsRegistry(defaults);
    for (const auto& f : defaultsReg.Fields())
    {
        settingDefaults_.emplace(f.key, f.save());
    }
}

// Anchors the vtable and the implicit-member destructor in this single TU (see header).
SettingsService::~SettingsService() = default;

// ── Settings getters ──────────────────────────────────────────────────────────

float SettingsService::GetSettingFloat(const char* key) const noexcept
{
    const SettingField* f = settingsRegistry_.Find(key);
    return (f && f->type == SettingType::Float && f->getFloat) ? f->getFloat() : 0.f;
}

bool SettingsService::GetSettingBool(const char* key) const noexcept
{
    const SettingField* f = settingsRegistry_.Find(key);
    return (f && f->type == SettingType::Bool && f->getBool) ? f->getBool() : false;
}

int SettingsService::GetSettingInt(const char* key) const noexcept
{
    const SettingField* f = settingsRegistry_.Find(key);
    return (f && f->type == SettingType::Int && f->getInt) ? f->getInt() : 0;
}

int SettingsService::GetSettingString(const char* key, char* buf, int cap) const noexcept
{
    const SettingField* f = settingsRegistry_.Find(key);
    const std::string val = (f && f->type == SettingType::String && f->getString) ? f->getString() : std::string();
    const int len = static_cast<int>(val.size());
    if (buf && cap > 0)
    {
        const int n = len < cap - 1 ? len : cap - 1;
        std::memcpy(buf, val.data(), static_cast<std::size_t>(n));
        buf[n] = '\0';
    }
    return len;
}

// ── Settings commit ───────────────────────────────────────────────────────────

void SettingsService::CommitSettingFloat(const char* key, float value) noexcept
{
    const SettingField* f = settingsRegistry_.Find(key);
    if (f && f->type == SettingType::Float && f->setFloat)
    {
        f->setFloat(value);
    }
}

void SettingsService::CommitSettingBool(const char* key, bool value) noexcept
{
    const SettingField* f = settingsRegistry_.Find(key);
    if (f && f->type == SettingType::Bool && f->setBool)
    {
        f->setBool(value);
    }
    if (std::strcmp(key, "playback.hwdec") == 0)
    {
        settings_->Get<PlaybackSettings>().hwdecMode =
            VideoDecodeModeName(value ? VideoDecodeMode::Auto : VideoDecodeMode::Off);
    }
}

void SettingsService::CommitSettingInt(const char* key, int value) noexcept
{
    const SettingField* f = settingsRegistry_.Find(key);
    if (f && f->type == SettingType::Int && f->setInt)
    {
        f->setInt(value);
    }
}

void SettingsService::CommitSettingString(const char* key, const char* value) noexcept
{
    if (!value)
    {
        return;
    }
    const SettingField* f = settingsRegistry_.Find(key);
    if (f && f->type == SettingType::String && f->setString)
    {
        f->setString(value);
    }
    if (std::strcmp(key, "playback.hwdecMode") == 0)
    {
        PlaybackSettings& pb = settings_->Get<PlaybackSettings>();
        pb.hwdecMode = VideoDecodeModeName(VideoDecodeModeFromString(pb.hwdecMode));
        pb.hwdec = IsVideoDecodeModeEnabled(VideoDecodeModeFromString(pb.hwdecMode));
    }
}

void SettingsService::SaveSettings() noexcept
{
    settings_->Save(settingsPath_);
    // Change callbacks run synchronously on the calling thread (the GUI thread, since
    // Save/Reload are driven from the settings UI). See the contract on
    // ISettingsStore::RegisterSettingsChangeCallback.
    for (const auto& rec : changeCallbacks_)
    {
        rec.cb(rec.ud);
    }
    for (const auto& ps : moduleSettings_ | std::views::values)
    {
        ps->Save();
    }
}

int SettingsService::GetSettingsFilePath(char* buf, int cap) const noexcept
{
    const int len = static_cast<int>(settingsPath_.size());
    if (buf && cap > 0)
    {
        const int n = len < cap - 1 ? len : cap - 1;
        std::memcpy(buf, settingsPath_.c_str(), static_cast<std::size_t>(n));
        buf[n] = '\0';
    }
    return len;
}

void SettingsService::ReloadSettings() noexcept
{
    // Reset to defaults first so a hand-deleted key reverts rather than retaining
    // its stale in-memory value — "what's in the file is the truth". Re-parse the
    // (possibly hand-edited) file, then re-apply via the same change-callback path
    // that SaveSettings uses (pushes settings into the player, theme, etc.).
    // ResetToDefaults() resets sections in place so settingsRegistry_ stays bound.
    settings_->ResetToDefaults();
    settings_->Load(settingsPath_);
    for (const auto& rec : changeCallbacks_)
    {
        rec.cb(rec.ud);
    }
}

void SettingsService::RegisterSettingsChangeCallback(void (*cb)(void*), void* ud, void (*cleanup)(void*)) noexcept
{
    changeCallbacks_.push_back({cb, ud, cleanup});
}

void SettingsService::EnumerateSettings(void (*visit)(const FrameLiftSettingDesc*, void*), void* visitUd) const noexcept
{
    if (!visit)
    {
        return;
    }
    for (const auto& f : settingsRegistry_.Fields())
    {
        const auto it = settingDefaults_.find(f.key);
        const char* defaultValue = it != settingDefaults_.end() ? it->second.c_str() : "";
        const FrameLiftSettingDesc desc{f.key.c_str(), static_cast<int>(f.type), f.desc.c_str(), defaultValue};
        visit(&desc, visitUd);
    }
}

// ── Module settings ───────────────────────────────────────────────────────────

IModuleSettings& SettingsService::GetModuleSettings(const char* sectionName) noexcept
{
    auto& ptr = moduleSettings_[sectionName];
    if (!ptr)
    {
        ptr = std::make_unique<ModuleSettingsImpl>(sectionName, settingsPath_);
    }
    return *ptr;
}

void SettingsService::RegisterModuleSetting(const FrameLiftModuleSettingDesc* desc) noexcept
{
    if (!desc || !desc->key || !desc->getValue || !desc->setValue)
    {
        return;
    }
    moduleSettingEntries_.push_back(
        {desc->key, desc->type, desc->desc ? desc->desc : "", desc->defaultValue ? desc->defaultValue : "",
         desc->getValue, desc->setValue, desc->ud}
    );
}

void SettingsService::EnumerateModuleSettings(
    void (*visit)(const FrameLiftModuleSettingDesc*, void*), void* visitUd
) const noexcept
{
    if (!visit)
    {
        return;
    }
    for (const auto& rec : moduleSettingEntries_)
    {
        const FrameLiftModuleSettingDesc desc{
            rec.key.c_str(), rec.type, rec.desc.c_str(), rec.defaultValue.c_str(), rec.getValue, rec.setValue, rec.ud
        };
        visit(&desc, visitUd);
    }
}

// ── Settings pages ────────────────────────────────────────────────────────────

void SettingsService::RegisterSettingsPage(
    const char* id, const char* title, const char* qmlUrl, QObject* viewModel, int order
) noexcept
{
    if (!id || !title || !qmlUrl || !viewModel)
    {
        return;
    }
    settingsPages_.push_back({id, title, qmlUrl, viewModel, order});
}

void SettingsService::EnumerateSettingsPages(
    void (*visit)(const FrameLiftSettingsPageDesc*, void*), void* visitUd
) const noexcept
{
    if (!visit)
    {
        return;
    }
    for (const auto& rec : settingsPages_)
    {
        const FrameLiftSettingsPageDesc desc{
            rec.id.c_str(), rec.title.c_str(), rec.qmlUrl.c_str(), rec.viewModel, rec.order
        };
        visit(&desc, visitUd);
    }
}

// ── Keybind entries ───────────────────────────────────────────────────────────

void SettingsService::RegisterKeybindEntry(
    const char* label, const char* actionName, const char* (*getStr)(void*), void (*setStr)(void*, const char*),
    void* ud, const char* group, const char* defaultBind
) noexcept
{
    keybindEntries_.push_back(
        {label, actionName, getStr, setStr, ud, group ? group : "", defaultBind ? defaultBind : ""}
    );
}

void SettingsService::EnumerateKeybindEntries(
    void (*visit)(
        const char*, const char*, const char* (*)(void*), void (*)(void*, const char*), void*, const char*, const char*,
        void*
    ),
    void* visitUd
) const noexcept
{
    for (const auto& e : keybindEntries_)
    {
        visit(
            e.label.c_str(), e.actionName.c_str(), e.getStr, e.setStr, e.ud, e.group.c_str(), e.defaultBind.c_str(),
            visitUd
        );
    }
}

// ── Cleanup ───────────────────────────────────────────────────────────────────

void SettingsService::ClearRegistrations()
{
    for (const auto& c : changeCallbacks_)
    {
        if (c.cleanup)
        {
            c.cleanup(c.ud);
        }
    }
    changeCallbacks_.clear();

    keybindEntries_.clear();
    moduleSettingEntries_.clear();
    settingsPages_.clear();
}
