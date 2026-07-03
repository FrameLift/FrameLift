#pragma once
#include <framelift/IModuleSettings.h>

// Host settings store: typed value get/commit, persistence, change notification,
// and per-module INI sections. A capability service — discover it with
// ctx.GetService<ISettingsStore>() and null-check before use.
class ISettingsStore
{
public:
    static constexpr const char* InterfaceId = "framelift.ISettingsStore";
    virtual ~ISettingsStore() = default;

    // ── Typed getters ──────────────────────────────────────────────────────────
    // Keys are "section.name" (e.g. "playback.hwdecMode", "ui.panelWidth").
    virtual float GetSettingFloat(const char* key) const noexcept = 0;
    virtual bool GetSettingBool(const char* key) const noexcept = 0;
    virtual int GetSettingInt(const char* key) const noexcept = 0;
    // Writes up to cap-1 chars + NUL into buf; returns full length excl. NUL.
    // Pass buf=nullptr to query the required length.
    virtual int GetSettingString(const char* key, char* buf, int cap) const noexcept = 0;

    // ── Commit (SettingsMenu only) ─────────────────────────────────────────────
    virtual void CommitSettingFloat(const char* key, float value) noexcept = 0;
    virtual void CommitSettingBool(const char* key, bool value) noexcept = 0;
    virtual void CommitSettingInt(const char* key, int value) noexcept = 0;
    virtual void CommitSettingString(const char* key, const char* value) noexcept = 0;
    // Persist all committed settings to disk and notify change subscribers.
    virtual void SaveSettings() noexcept = 0;

    // Register a callback invoked after settings are committed. cleanup(ud) is
    // called on plugin unload.
    //
    // Thread affinity: the callback runs SYNCHRONOUSLY on the thread that calls
    // SaveSettings()/ReloadSettings() — in practice the GUI thread, since both are
    // driven from the settings UI. It is therefore safe to touch QML/UI state from
    // the callback; conversely, do not invoke Save/Reload off the GUI thread.
    virtual void RegisterSettingsChangeCallback(
        void (*cb)(void* ud), void* ud, void (*cleanup)(void* ud) = nullptr
    ) noexcept = 0;

    // Per-plugin settings (its own INI section).
    virtual IModuleSettings& GetModuleSettings(const char* sectionName) noexcept = 0;

    // Absolute path of the on-disk settings.ini. Returns full length excl. NUL;
    // pass buf=nullptr to query the required size.
    virtual int GetSettingsFilePath(char* buf, int cap) const noexcept = 0;
    // Re-read settings.ini from disk into the live settings and re-apply them
    // (fires the settings-change callbacks). Use after the file is edited directly.
    virtual void ReloadSettings() noexcept = 0;
};
