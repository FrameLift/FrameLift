#pragma once

#include <framelift/core.h>
#include <framelift/ui.h>

// SettingsMenu owns the authoritative Settings struct as its internal editing
// model and compiles src/Settings.cpp into itself. Settings is host-internal
// (not part of the public SDK), so it is included from src/ directly.
#include <Settings.h>

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// ── Settings dialog ───────────────────────────────────────────────────────────
// Centered modal-like ImGui window. Call Open() to show; renders while open.
class SettingsMenu final : public SafeRenderable, public PluginBase
{
public:
    // Call once with the pref-dir path so the menu can load/save settings.
    void SetStoragePath(std::string path);

    bool HandleKeyDownEvent(const AppEvent& e) override;

    // Make the settings dialog visible (acquires keyboard focus and publishes
    // SettingsVisibilityEvent).
    void Open() noexcept;

    // Hide the settings dialog (releases keyboard focus and publishes
    // SettingsVisibilityEvent).
    void Close() noexcept;

    [[nodiscard]] bool IsOpen() const noexcept
    {
        return open_;
    }

    // Return the live settings. Safe to hold a pointer for the application
    // lifetime — the object never moves. Changes are visible immediately.
    [[nodiscard]] const Settings& GetSettings() const
    {
        return settings_;
    }

    // Register a callback invoked after settings are saved to disk.
    void RegisterChangeCallback(std::function<void(const Settings&)> cb);

    void OnRender(UIContext& ctx) override;

protected:
    const char* PluginName() const override
    {
        return "SettingsMenu";
    }

    std::vector<framelift::Keybind> Keybinds() override;
    void OnInstall(IPluginContext& ctx) override;

private:
    // ── Core pages ──────────────────────────────────────────────────────────────
    // The six built-in pages register through the same host pipeline as plugin
    // pages. Each descriptor carries a stable address used both as the page's
    // user-data pointer and to classify a page as "core" (see IsCorePageUd).
    struct CorePage
    {
        SettingsMenu* self;
        void (SettingsMenu::*render)(UIContext&);
        const char* title;
        const char* resetSection; // settings-struct section reset by "this page only"
    };

    // Render thunk passed to RegisterSettingsPage for every core page.
    static void CoreRenderThunk(void* ud, UIContext& ctx);

    [[nodiscard]] bool IsCorePageUd(const void* ud) const noexcept
    {
        const auto* base = corePages_.data();
        return ud >= base && ud < base + corePages_.size();
    }

    void RegisterCorePages(IPluginContext& ctx);

    void RenderSidebar(UIContext& ctx);
    void RenderPageGeneral(UIContext& ctx);
    void RenderPageGraphics(UIContext& ctx);
    void RenderPagePlayback(UIContext& ctx);
    void RenderPageSubtitles(UIContext& ctx);
    void RenderPageCache(UIContext& ctx);
    void RenderPageUI(UIContext& ctx);
    void RenderPageTheme(UIContext& ctx);
    void RenderPageFiles(UIContext& ctx);
    void RenderPageAudio(UIContext& ctx);
    void RenderPageKeybinds(UIContext& ctx);
    void RenderPagePlugins(UIContext& ctx);
    void RenderPageConfig(UIContext& ctx);

    // Seed/refresh the typed editing model (settings_) from the host via the
    // ABI-stable per-key getters. Called once on install and again after a raw
    // config edit is saved so the typed pages reflect the new on-disk values.
    void SeedFromContext(IPluginContext& ctx);

    // Read settings.ini (path resolved via the host) into configText_ for the raw
    // editor; truncates with a flag if the file exceeds the buffer.
    void LoadConfigText();

    // Lazily fetch the system font catalogue from the host (once per session).
    void EnsureFontsQueried();

    void Save();
    void Reset();
    void ResetPage();
    void FireChangeCallbacks() const;
    void SetCapturing(bool v);
    [[nodiscard]] const char* PageName() const;

    // ── State ──────────────────────────────────────────────────────────────────
    bool open_ = false;
    bool dirty_ = false;       // true when settings_ differs from saved_
    bool isCapturing_ = false; // true while waiting for a key press to rebind
    int activePageIndex_ = -1; // index into EnumerateSettingsPages(), -1 = resolve on Open

    // Built-in pages, registered through the host settings pipeline in OnInstall.
    std::array<CorePage, 12> corePages_{};

    std::string openSettingsKey_ = "Ctrl+Comma";

    // System font catalogue for the Theme page, fetched once via the host ABI.
    // Index 0 is always the "Default" entry (empty path = ImGui default font).
    bool fontsQueried_ = false;
    std::vector<std::string> fontNames_;
    std::vector<std::string> fontPaths_;

    Settings settings_; // live/authoritative — shared via GetSettings() pointer
    Settings saved_;    // last snapshot written to disk

    std::string storagePath_;

    // ── Raw config editor (Config page) ─────────────────────────────────────────
    std::string configPath_;       // absolute settings.ini path, resolved from the host
    std::vector<char> configText_; // NUL-terminated edit buffer (fixed 64 KB cap)
    bool configLoaded_ = false;    // file slurped into configText_ at least once
    bool configTruncated_ = false; // file was larger than the buffer

    std::string capturingName_; // action name of the keybind being rebound

    // For core keybinds: points into a settings_ string field.
    std::string* capturingBind_ = nullptr;
    // For plugin keybinds: fn-ptr accessors.
    const char* (*capturingGetStr_)(void*) = nullptr;
    void (*capturingSetStr_)(void*, const char*) = nullptr;
    void* capturingUd_ = nullptr;

    std::vector<std::function<void(const Settings&)>> changeCallbacks_;
};