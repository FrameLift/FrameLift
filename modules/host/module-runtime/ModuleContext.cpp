#include "ModuleContext.h"
#include "PackageConfig.h"
#include "Settings.h"
#include "PlaybackSettings.h"
#include "VideoDecodeMode.h"
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <memory>
#include <ranges>
#include <string>
#include <utility>

ModuleContext::ModuleContext(
    std::string prefPath, Settings* settings, const std::string& settingsPath, PackageConfig* packageConfig,
    std::string packagesPath
)
    : prefPath_(std::move(prefPath)), settingsPath_(settingsPath), settings_(settings), packageConfig_(packageConfig),
      packagesPath_(std::move(packagesPath))
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

    // Register every host capability the context itself implements so plugins reach
    // them via ctx.GetService<T>(). The vtable of IModuleContext stays at the four
    // bootstrap methods; these are the discoverable Tier-2 services.
    RegisterServiceRaw(ISettingsStore::InterfaceId, static_cast<ISettingsStore*>(this));
    RegisterServiceRaw(ISettingsRegistry::InterfaceId, static_cast<ISettingsRegistry*>(this));
    RegisterServiceRaw(IPackageCatalog::InterfaceId, static_cast<IPackageCatalog*>(this));
    RegisterServiceRaw(IFontCatalog::InterfaceId, static_cast<IFontCatalog*>(this));
    RegisterServiceRaw(IAppPaths::InterfaceId, static_cast<IAppPaths*>(this));
}

// ── Pref path ─────────────────────────────────────────────────────────────────

int ModuleContext::GetPrefPath(char* buf, int cap) const noexcept
{
    const int len = static_cast<int>(prefPath_.size());
    if (buf && cap > 0)
    {
        const int n = len < cap - 1 ? len : cap - 1;
        std::memcpy(buf, prefPath_.c_str(), static_cast<std::size_t>(n));
        buf[n] = '\0';
    }
    return len;
}

// ── Package catalogue ─────────────────────────────────────────────────────────

void ModuleContext::AddPackage(std::string name, bool enabled, const FrameLiftPackageInfo* info)
{
    // A package present but not loaded while enabled was attempted and failed at
    // startup. Computed once here; SetPackageEnabled toggles never revise it, so a
    // freshly enabled package reads as "pending restart", not "failed".
    const bool loadFailed = !info && enabled;
    packageCatalog_.push_back({std::move(name), enabled, loadFailed, info});
}

void ModuleContext::EnumeratePackages(
    void (*visit)(const char*, const FrameLiftPackageInfo&, bool, bool, bool, void*), void* visitUd
) const noexcept
{
    if (!visit)
    {
        return;
    }
    for (const auto& rec : packageCatalog_)
    {
        if (rec.info)
        {
            visit(rec.name.c_str(), *rec.info, rec.enabled, true, rec.loadFailed, visitUd);
        }
        else
        {
            // Synthesize a name-only descriptor for a present-but-disabled DLL.
            const FrameLiftPackageInfo synth{
                0, rec.name.c_str(), rec.name.c_str(), rec.name.c_str(), {0, 0, 0}, nullptr, nullptr, nullptr, 0};
            visit(rec.name.c_str(), synth, rec.enabled, false, rec.loadFailed, visitUd);
        }
    }
}

void ModuleContext::SetPackageEnabled(const char* name, bool enabled) noexcept
{
    if (!name)
    {
        return;
    }

    const auto rec = std::ranges::find_if(packageCatalog_, [&](const PackageRec& r) { return r.name == name; });
    if (rec == packageCatalog_.end())
    {
        return; // unknown package
    }
    rec->enabled = enabled; // reflect immediately so the UI checkbox updates

    // Persist to the opt-out manifest; the change takes effect on the next launch,
    // so there is no live state to notify (skip the change-callback fan-out).
    if (packageConfig_)
    {
        packageConfig_->Set(rec->name, enabled);
        packageConfig_->Save(packagesPath_);
    }
}

void ModuleContext::EnumerateSystemFonts(void (*visit)(const char*, const char*, void*), void* visitUd) const noexcept
{
    if (!visit)
    {
        return;
    }
    if (!fontsScanned_)
    {
        fontCache_ = ScanFontDirs(SystemFontDirs());
        fontsScanned_ = true;
    }
    for (const auto& f : fontCache_)
    {
        visit(f.name.c_str(), f.path.c_str(), visitUd);
    }
}

// ── Settings getters ──────────────────────────────────────────────────────────

float ModuleContext::GetSettingFloat(const char* key) const noexcept
{
    const SettingField* f = settingsRegistry_.Find(key);
    return (f && f->type == SettingType::Float && f->getFloat) ? f->getFloat() : 0.f;
}

bool ModuleContext::GetSettingBool(const char* key) const noexcept
{
    const SettingField* f = settingsRegistry_.Find(key);
    return (f && f->type == SettingType::Bool && f->getBool) ? f->getBool() : false;
}

int ModuleContext::GetSettingInt(const char* key) const noexcept
{
    const SettingField* f = settingsRegistry_.Find(key);
    return (f && f->type == SettingType::Int && f->getInt) ? f->getInt() : 0;
}

int ModuleContext::GetSettingString(const char* key, char* buf, int cap) const noexcept
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

void ModuleContext::CommitSettingFloat(const char* key, float value) noexcept
{
    const SettingField* f = settingsRegistry_.Find(key);
    if (f && f->type == SettingType::Float && f->setFloat)
    {
        f->setFloat(value);
    }
}

void ModuleContext::CommitSettingBool(const char* key, bool value) noexcept
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

void ModuleContext::CommitSettingInt(const char* key, int value) noexcept
{
    const SettingField* f = settingsRegistry_.Find(key);
    if (f && f->type == SettingType::Int && f->setInt)
    {
        f->setInt(value);
    }
}

void ModuleContext::CommitSettingString(const char* key, const char* value) noexcept
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

void ModuleContext::SaveSettings() noexcept
{
    settings_->Save(settingsPath_);
    for (const auto& rec : changeCallbacks_)
    {
        rec.cb(rec.ud);
    }
    for (const auto& ps : moduleSettings_ | std::views::values)
    {
        ps->Save();
    }
}

int ModuleContext::GetSettingsFilePath(char* buf, int cap) const noexcept
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

void ModuleContext::ReloadSettings() noexcept
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

void ModuleContext::RegisterSettingsChangeCallback(void (*cb)(void*), void* ud, void (*cleanup)(void*)) noexcept
{
    changeCallbacks_.push_back({cb, ud, cleanup});
}

void ModuleContext::EnumerateSettings(
    void (*visit)(const FrameLiftSettingDesc*, void*), void* visitUd
) const noexcept
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

IModuleSettings& ModuleContext::GetModuleSettings(const char* sectionName) noexcept
{
    auto& ptr = moduleSettings_[sectionName];
    if (!ptr)
    {
        ptr = std::make_unique<ModuleSettingsImpl>(sectionName, settingsPath_);
    }
    return *ptr;
}

// ── Settings pages ────────────────────────────────────────────────────────────

void ModuleContext::RegisterSettingsPage(
    const char* title, void (*renderFn)(void*, UIContext&), void (*applyFn)(void*), void* ud, bool visible,
    void (*cleanup)(void*)
) noexcept
{
    settingsPages_.push_back({title, renderFn, applyFn, ud, visible, cleanup});
}

void ModuleContext::EnumerateSettingsPages(
    void (*visit)(const char*, void (*)(void*, UIContext&), void (*)(void*), void*, bool, void*), void* visitUd
) const noexcept
{
    for (const auto& p : settingsPages_)
    {
        visit(p.title.c_str(), p.renderFn, p.applyFn, p.ud, p.visible, visitUd);
    }
}

// ── Keybind entries ───────────────────────────────────────────────────────────

void ModuleContext::RegisterKeybindEntry(
    const char* label, const char* actionName, const char* (*getStr)(void*), void (*setStr)(void*, const char*),
    void* ud
) noexcept
{
    keybindEntries_.push_back({label, actionName, getStr, setStr, ud});
}

void ModuleContext::EnumerateKeybindEntries(
    void (*visit)(const char*, const char*, const char* (*)(void*), void (*)(void*, const char*), void*, void*),
    void* visitUd
) const noexcept
{
    for (const auto& e : keybindEntries_)
    {
        visit(e.label.c_str(), e.actionName.c_str(), e.getStr, e.setStr, e.ud, visitUd);
    }
}

// ── Service registry ──────────────────────────────────────────────────────────

void* ModuleContext::GetServiceRaw(const char* id) const noexcept
{
    const auto it = registry_.find(id);
    return it != registry_.end() ? it->second : nullptr;
}

void ModuleContext::RegisterServiceRaw(const char* id, void* ptr) noexcept
{
    registry_[id] = ptr;
}

// ── Pub/sub ───────────────────────────────────────────────────────────────────

void ModuleContext::SubscribeRaw(
    const char* id, void (*cb)(const void*, void*), void* ud, void (*cleanup)(void*)
) noexcept
{
    subscriptions_.push_back({id, cb, ud, cleanup});
}

void ModuleContext::PublishRaw(const char* id, const void* payload) noexcept
{
    // Reentrancy-safe: a callback may SubscribeRaw() (the vector may reallocate)
    // or even ClearSubscriptions(). Index + per-step bound check instead of
    // iterators; subscribers added during dispatch do not see the in-flight
    // event (count snapshot), and cb/ud are copied out before the call so a
    // reallocation inside the callback cannot invalidate them.
    const std::size_t count = subscriptions_.size();
    for (std::size_t i = 0; i < count && i < subscriptions_.size(); ++i)
    {
        if (subscriptions_[i].eventId == id)
        {
            const auto cb = subscriptions_[i].cb;
            void* const ud = subscriptions_[i].ud;
            cb(payload, ud);
        }
    }
}

// ── Cleanup ───────────────────────────────────────────────────────────────────

void ModuleContext::ClearSubscriptions()
{
    for (const auto& s : subscriptions_)
    {
        if (s.cleanup)
        {
            s.cleanup(s.ud);
        }
    }
    subscriptions_.clear();

    for (const auto& c : changeCallbacks_)
    {
        if (c.cleanup)
        {
            c.cleanup(c.ud);
        }
    }
    changeCallbacks_.clear();

    for (const auto& p : settingsPages_)
    {
        if (p.cleanup)
        {
            p.cleanup(p.ud);
        }
    }
    settingsPages_.clear();

    keybindEntries_.clear();
}
