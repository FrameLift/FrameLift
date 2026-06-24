#pragma once

#include <framelift/core.h>
#include <framelift/services.h>
#include <framelift/ui.h>

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// ── Key-based editing model ───────────────────────────────────────────────────
// SettingsMenu is fully decoupled from the host Settings layout: it discovers the
// available fields over the ABI (IModuleContext::EnumerateSettings) and edits a
// typed value store keyed by "section.name". Accessors return references so the
// existing widget call sites can bind directly; lookups default-insert, and
// std::unordered_map keeps those references stable across later inserts.
class EditModel
{
public:
    void Clear()
    {
        bools_.clear();
        ints_.clear();
        floats_.clear();
        strings_.clear();
    }

    bool& Bool(const std::string& key)
    {
        return bools_[key];
    }
    int& Int(const std::string& key)
    {
        return ints_[key];
    }
    float& Float(const std::string& key)
    {
        return floats_[key];
    }
    std::string& Str(const std::string& key)
    {
        return strings_[key];
    }

private:
    std::unordered_map<std::string, bool> bools_;
    std::unordered_map<std::string, int> ints_;
    std::unordered_map<std::string, float> floats_;
    std::unordered_map<std::string, std::string> strings_;
};

// ── Settings dialog ───────────────────────────────────────────────────────────
// Centered modal-like ImGui window. Call Open() to show; renders while open.
class SettingsMenu final : public SafeRenderable, public ModuleBase
{
public:
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

    // Read the current edited value for a "section.name" key. Used by tests.
    [[nodiscard]] bool SettingBool(const std::string& key)
    {
        return model_.Bool(key);
    }
    [[nodiscard]] int SettingInt(const std::string& key)
    {
        return model_.Int(key);
    }
    [[nodiscard]] float SettingFloat(const std::string& key)
    {
        return model_.Float(key);
    }
    [[nodiscard]] std::string SettingString(const std::string& key)
    {
        return model_.Str(key);
    }

    void OnRender(UIContext& ctx) override;

protected:
    const char* ModuleName() const override
    {
        return "SettingsMenu";
    }

    std::vector<framelift::Keybind> Keybinds() override;
    void OnInstall(IModuleContext& ctx) override;

private:
    // ── Core pages ──────────────────────────────────────────────────────────────
    // The built-in pages register through the same host pipeline as plugin pages.
    // Each descriptor carries a stable address used both as the page's user-data
    // pointer and to classify a page as "core" (see IsCorePageUd).
    struct CorePage
    {
        SettingsMenu* self;
        void (SettingsMenu::*render)(UIContext&);
        const char* title;
        const char* resetSection; // settings section reset by "this page only"
    };

    // One discovered settings field, from EnumerateSettings.
    struct FieldMeta
    {
        std::string key;
        int type; // 0 bool, 1 int, 2 float, 3 string (FrameLiftSettingDesc::type)
        std::string defaultValue;
    };

    // Render thunk passed to RegisterSettingsPage for every core page.
    static void CoreRenderThunk(void* ud, UIContext& ctx);

    [[nodiscard]] bool IsCorePageUd(const void* ud) const noexcept
    {
        const auto* base = corePages_.data();
        return ud >= base && ud < base + corePages_.size();
    }

    // Host capability services, fetched from ctx_ on demand. The host registers all
    // of them, so these return non-null once the module is installed.
    [[nodiscard]] ISettingsStore* SettingsStore() const
    {
        return ctx_ ? ctx_->GetService<ISettingsStore>() : nullptr;
    }
    [[nodiscard]] ISettingsRegistry* SettingsReg() const
    {
        return ctx_ ? ctx_->GetService<ISettingsRegistry>() : nullptr;
    }
    [[nodiscard]] IPackageCatalog* PackageCatalog() const
    {
        return ctx_ ? ctx_->GetService<IPackageCatalog>() : nullptr;
    }
    [[nodiscard]] IFontCatalog* FontCatalog() const
    {
        return ctx_ ? ctx_->GetService<IFontCatalog>() : nullptr;
    }

    void RegisterCorePages(IModuleContext& ctx);

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

    // Discover the settings fields over the ABI and seed the editing model with the
    // host's current values. Called once on install and again after a raw config
    // edit is saved so the typed pages reflect the new on-disk values.
    void SeedFromContext(IModuleContext& ctx);
    void SeedValue(IModuleContext& ctx, const FieldMeta& f);
    void ResetValue(const FieldMeta& f);

    // Read settings.ini (path resolved via the host) into configText_ for the raw
    // editor; truncates with a flag if the file exceeds the buffer.
    void LoadConfigText();

    // Lazily fetch the system font catalogue from the host (once per session).
    void EnsureFontsQueried();

    void Save();
    void Reset();
    void ResetPage();
    void SetCapturing(bool v);
    [[nodiscard]] const char* PageName() const;

    // ── State ──────────────────────────────────────────────────────────────────
    bool open_ = false;
    bool dirty_ = false;       // true when the model differs from the saved values
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

    // Editing model: every host settings field discovered over the ABI, plus the
    // live edited values keyed by "section.name".
    std::vector<FieldMeta> fields_;
    EditModel model_;

    // ── Raw config editor (Config page) ─────────────────────────────────────────
    std::string configPath_;       // absolute settings.ini path, resolved from the host
    std::vector<char> configText_; // NUL-terminated edit buffer (fixed 64 KB cap)
    bool configLoaded_ = false;    // file slurped into configText_ at least once
    bool configTruncated_ = false; // file was larger than the buffer

    std::string capturingName_; // action name of the keybind being rebound
    int capturingSlot_ = 0;     // which slot (0 primary, 1 alternate) is being rebound

    // For core keybinds: points into the model's keybind string value.
    std::string* capturingBind_ = nullptr;
    // For plugin keybinds: fn-ptr accessors.
    const char* (*capturingGetStr_)(void*) = nullptr;
    void (*capturingSetStr_)(void*, const char*) = nullptr;
    void* capturingUd_ = nullptr;

    // Transient warning shown on the Keybinds page when a capture is rejected
    // because the pressed key is already bound to a different action. Cleared on
    // the next capture start or successful edit.
    std::string keybindConflict_;

    // Returns the label of whichever keybind currently holds `canonicalKey`, or an
    // empty string if none — ignoring `exceptAction` (the action being edited).
    // Scans both core ("keybinds.*") and plugin-registered keybind entries.
    [[nodiscard]] std::string FindKeyOwnerLabel(const std::string& canonicalKey, const char* exceptAction);
};

FRAMELIFT_MODULE_ENTRY(SettingsMenu, {
    .renderOrder = 50,
})
