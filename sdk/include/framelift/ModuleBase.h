#pragma once
#include <cctype>
#include <framelift/IModule.h>
#include <framelift/IModuleContext.h>
#include <framelift/IModuleSettings.h>
#include <framelift/ModuleKeybinds.h>
#include <string>
#include <vector>

class Hotkeys;

// Convenience base class for app modules. IModule itself is intentionally tiny;
// ModuleBase opts into the common host dispatch surfaces and guards/forwards
// them to override-friendly hooks.
class ModuleBase : public IModule,
                   public IHotkeyProvider,
                   public IEventHandler,
                   public IMediaEventHandler,
                   public IShutdownHandler
{
public:
    // Stores ctx_, loads settings, registers keybind metadata, then calls
    // OnInstall for module-specific setup.
    void Install(IModuleContext& ctx) noexcept final;

    void Uninstall() noexcept final;
    void* QueryInterface(const char* interfaceId) noexcept final;

    void BindHotkeys(Hotkeys& keys) noexcept final;
    bool OnEvent(const AppEvent& e) noexcept final;
    void OnMediaEvent(const MediaEvent& e) noexcept final;
    void OnShutdown() noexcept final;

protected:
    // Required: display/settings name used by the current host settings UI.
    // Future module metadata plumbing can remove this virtual from C++ modules.
    virtual const char* ModuleName() const = 0;

    virtual std::vector<framelift::Keybind> Keybinds()
    {
        return {};
    }

    virtual bool HandleEvent(const AppEvent& e);

    virtual bool HandleKeyDownEvent(const AppEvent&)
    {
        return false;
    }

    virtual void HandleMediaEvent(const MediaEvent&)
    {
    }

    virtual void HandleShutdown()
    {
    }

    virtual void LoadSettings(IModuleSettings& ps);
    virtual void RegisterKeybinds(IModuleContext& ctx);

    virtual void OnInstall(IModuleContext& ctx)
    {
    }

    virtual void OnUninstall()
    {
    }

    virtual void SaveSettings(IModuleSettings& ps);
    virtual void LoadKeybinds(IModuleSettings& kps);
    virtual void SaveKeybinds(IModuleSettings& kps);
    virtual void OnBindHotkeys(Hotkeys& keys);

    [[nodiscard]] std::string PrefixedKey(const char* name) const
    {
        return std::string(ModuleName()) + "." + name;
    }

    [[nodiscard]] std::string SettingsSection() const
    {
        std::string s = ModuleName();
        if (!s.empty())
        {
            s[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[0])));
        }
        return s;
    }

    [[nodiscard]] std::string KeybindsSection() const
    {
        return SettingsSection() + ".keybinds";
    }

    IModuleContext* ctx_ = nullptr;

private:
    void PersistSettings();

    std::vector<framelift::Keybind> keybinds_;
};
