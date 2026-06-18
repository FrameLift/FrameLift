#include "PluginContext.h"
#include "Settings.h"
#include "platform/ffmpeg/VideoDecodeMode.h"
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <memory>
#include <ranges>
#include <string>
#include <utility>

PluginContext::PluginContext(std::string prefPath, Settings* settings, const std::string& settingsPath)
    : prefPath_(std::move(prefPath)), settingsPath_(settingsPath), settings_(settings)
{
}

// ── Pref path ─────────────────────────────────────────────────────────────────

int PluginContext::GetPrefPath(char* buf, int cap) const noexcept
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

// ── Plugin catalogue ──────────────────────────────────────────────────────────

void PluginContext::AddPlugin(std::string name, bool enabled, const FrameLiftPluginInfo* info)
{
    // A plugin present but not loaded while enabled was attempted and failed at
    // startup. Computed once here; SetPluginEnabled toggles never revise it, so a
    // freshly enabled plugin reads as "pending restart", not "failed".
    const bool loadFailed = !info && enabled;
    pluginCatalog_.push_back({std::move(name), enabled, loadFailed, info});
}

void PluginContext::EnumeratePlugins(
    void (*visit)(const char*, const FrameLiftPluginInfo&, bool, bool, bool, void*), void* visitUd
) const noexcept
{
    if (!visit)
    {
        return;
    }
    for (const auto& rec : pluginCatalog_)
    {
        if (rec.info)
        {
            visit(rec.name.c_str(), *rec.info, rec.enabled, true, rec.loadFailed, visitUd);
        }
        else
        {
            // Synthesize a name-only descriptor for a present-but-disabled DLL.
            const FrameLiftPluginInfo synth{0, 0, 0, rec.name.c_str(), {0, 0, 0}, nullptr, nullptr};
            visit(rec.name.c_str(), synth, rec.enabled, false, rec.loadFailed, visitUd);
        }
    }
}

void PluginContext::SetPluginEnabled(const char* name, bool enabled) noexcept
{
    if (!name)
    {
        return;
    }

    const auto rec = std::ranges::find_if(
        pluginCatalog_,
        [&](const PluginRec& r)
        {
            return r.name == name;
        }
    );
    if (rec == pluginCatalog_.end())
    {
        return; // unknown plugin
    }
    rec->enabled = enabled; // reflect immediately so the UI checkbox updates

    auto& list = settings_->enabledPlugins;
    const auto it = std::ranges::find(list, rec->name);
    if (enabled && it == list.end())
    {
        list.push_back(rec->name);
    }
    else if (!enabled && it != list.end())
    {
        list.erase(it);
    }

    // Persist the enabled list only; the toggle takes effect on next launch, so
    // there is no live state to notify (skip the change-callback fan-out).
    settings_->Save(settingsPath_);
}

void PluginContext::EnumerateSystemFonts(void (*visit)(const char*, const char*, void*), void* visitUd) const noexcept
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

namespace
{
// Visit every settings field, invoking fn("section.name", fieldRef) for each.
// fn is a GENERIC lambda so the per-field `if constexpr` is dependent on the
// field type and the mismatched branches are correctly discarded (this would NOT
// hold in a non-template function — float.c_str() etc. would be hard errors).
template <typename S, typename Fn>
void ForEachField(S&& settings, Fn&& fn)
{
#define X(section, name, type, def, desc) fn(#section "." #name, settings.name);
    SETTINGS_FIELDS(X)
#undef X
}
} // namespace

float PluginContext::GetSettingFloat(const char* key) const noexcept
{
    float result = 0.f;
    ForEachField(
        *settings_,
        [&]<typename T0>(const char* k, const T0& field)
        {
            if constexpr (std::is_same_v<std::decay_t<T0>, float>)
            {
                if (std::strcmp(key, k) == 0)
                {
                    result = field;
                }
            }
        }
    );
    return result;
}

bool PluginContext::GetSettingBool(const char* key) const noexcept
{
    bool result = false;
    ForEachField(
        *settings_,
        [&]<typename T0>(const char* k, const T0& field)
        {
            if constexpr (std::is_same_v<std::decay_t<T0>, bool>)
            {
                if (std::strcmp(key, k) == 0)
                {
                    result = field;
                }
            }
        }
    );
    return result;
}

int PluginContext::GetSettingInt(const char* key) const noexcept
{
    int result = 0;
    ForEachField(
        *settings_,
        [&]<typename T0>(const char* k, const T0& field)
        {
            if constexpr (std::is_same_v<std::decay_t<T0>, int>)
            {
                if (std::strcmp(key, k) == 0)
                {
                    result = field;
                }
            }
        }
    );
    return result;
}

int PluginContext::GetSettingString(const char* key, char* buf, int cap) const noexcept
{
    const char* val = "";
    ForEachField(
        *settings_,
        [&]<typename T0>(const char* k, const T0& field)
        {
            if constexpr (std::is_same_v<std::decay_t<T0>, std::string>)
            {
                if (std::strcmp(key, k) == 0)
                {
                    val = field.c_str();
                }
            }
        }
    );
    const int len = static_cast<int>(std::strlen(val));
    if (buf && cap > 0)
    {
        const int n = len < cap - 1 ? len : cap - 1;
        std::memcpy(buf, val, static_cast<std::size_t>(n));
        buf[n] = '\0';
    }
    return len;
}

// ── Settings commit ───────────────────────────────────────────────────────────

void PluginContext::CommitSettingFloat(const char* key, float value) noexcept
{
    ForEachField(
        *settings_,
        [&]<typename T0>(const char* k, T0& field)
        {
            if constexpr (std::is_same_v<std::decay_t<T0>, float>)
            {
                if (std::strcmp(key, k) == 0)
                {
                    field = value;
                }
            }
        }
    );
}

void PluginContext::CommitSettingBool(const char* key, bool value) noexcept
{
    ForEachField(
        *settings_,
        [&]<typename T0>(const char* k, T0& field)
        {
            if constexpr (std::is_same_v<std::decay_t<T0>, bool>)
            {
                if (std::strcmp(key, k) == 0)
                {
                    field = value;
                }
            }
        }
    );
    if (std::strcmp(key, "playback.hwdec") == 0)
    {
        settings_->hwdecMode = VideoDecodeModeName(value ? VideoDecodeMode::Auto : VideoDecodeMode::Off);
    }
}

void PluginContext::CommitSettingInt(const char* key, int value) noexcept
{
    ForEachField(
        *settings_,
        [&]<typename T0>(const char* k, T0& field)
        {
            if constexpr (std::is_same_v<std::decay_t<T0>, int>)
            {
                if (std::strcmp(key, k) == 0)
                {
                    field = value;
                }
            }
        }
    );
}

void PluginContext::CommitSettingString(const char* key, const char* value) noexcept
{
    if (!value)
    {
        return;
    }
    ForEachField(
        *settings_,
        [&]<typename T0>(const char* k, T0& field)
        {
            if constexpr (std::is_same_v<std::decay_t<T0>, std::string>)
            {
                if (std::strcmp(key, k) == 0)
                {
                    field = value;
                }
            }
        }
    );
    if (std::strcmp(key, "playback.hwdecMode") == 0)
    {
        settings_->hwdecMode = VideoDecodeModeName(VideoDecodeModeFromString(settings_->hwdecMode));
        settings_->hwdec = IsVideoDecodeModeEnabled(VideoDecodeModeFromString(settings_->hwdecMode));
    }
}

void PluginContext::SaveSettings() noexcept
{
    settings_->Save(settingsPath_);
    for (const auto& rec : changeCallbacks_)
    {
        rec.cb(rec.ud);
    }
    for (const auto& ps : pluginSettings_ | std::views::values)
    {
        ps->Save();
    }
}

void PluginContext::CommitSettingsDirect(const Settings& s)
{
    *settings_ = s;
    SaveSettings();
}

int PluginContext::GetSettingsFilePath(char* buf, int cap) const noexcept
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

void PluginContext::ReloadSettings() noexcept
{
    // Reset to defaults first so a hand-deleted key reverts rather than retaining
    // its stale in-memory value — "what's in the file is the truth". Re-parse the
    // (possibly hand-edited) file, then re-apply via the same change-callback path
    // that SaveSettings uses (pushes settings into the player, theme, etc.).
    *settings_ = Settings{};
    settings_->Load(settingsPath_);
    for (const auto& rec : changeCallbacks_)
    {
        rec.cb(rec.ud);
    }
}

void PluginContext::RegisterSettingsChangeCallback(void (*cb)(void*), void* ud, void (*cleanup)(void*)) noexcept
{
    changeCallbacks_.push_back({cb, ud, cleanup});
}

// ── Plugin settings ───────────────────────────────────────────────────────────

IPluginSettings& PluginContext::GetPluginSettings(const char* sectionName) noexcept
{
    auto& ptr = pluginSettings_[sectionName];
    if (!ptr)
    {
        ptr = std::make_unique<PluginSettingsImpl>(sectionName, settingsPath_);
    }
    return *ptr;
}

// ── Settings pages ────────────────────────────────────────────────────────────

void PluginContext::RegisterSettingsPage(
    const char* title, void (*renderFn)(void*, UIContext&), void (*applyFn)(void*), void* ud, bool visible,
    void (*cleanup)(void*)
) noexcept
{
    settingsPages_.push_back({title, renderFn, applyFn, ud, visible, cleanup});
}

void PluginContext::EnumerateSettingsPages(
    void (*visit)(const char*, void (*)(void*, UIContext&), void (*)(void*), void*, bool, void*), void* visitUd
) const noexcept
{
    for (const auto& p : settingsPages_)
    {
        visit(p.title.c_str(), p.renderFn, p.applyFn, p.ud, p.visible, visitUd);
    }
}

// ── Keybind entries ───────────────────────────────────────────────────────────

void PluginContext::RegisterKeybindEntry(
    const char* label, const char* actionName, const char* (*getStr)(void*), void (*setStr)(void*, const char*),
    void* ud
) noexcept
{
    keybindEntries_.push_back({label, actionName, getStr, setStr, ud});
}

void PluginContext::EnumerateKeybindEntries(
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

void* PluginContext::GetServiceRaw(const char* id) const noexcept
{
    const auto it = registry_.find(id);
    return it != registry_.end() ? it->second : nullptr;
}

void PluginContext::RegisterServiceRaw(const char* id, void* ptr) noexcept
{
    registry_[id] = ptr;
}

// ── Pub/sub ───────────────────────────────────────────────────────────────────

void PluginContext::SubscribeRaw(
    const char* id, void (*cb)(const void*, void*), void* ud, void (*cleanup)(void*)
) noexcept
{
    subscriptions_.push_back({id, cb, ud, cleanup});
}

void PluginContext::PublishRaw(const char* id, const void* payload) noexcept
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

void PluginContext::ClearSubscriptions()
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
