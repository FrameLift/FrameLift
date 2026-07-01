#include <cstring>
#include <framelift/ContextHelpers.h>
#include <framelift/Guard.h>
#include <framelift/HotkeyHelpers.h>
#include <framelift/ModuleBase.h>
#include <framelift/services/ISettingsRegistry.h>
#include <framelift/services/ISettingsStore.h>

void ModuleBase::Install(IModuleContext& ctx) noexcept
{
    ctx_ = &ctx;

    framelift::Guard(
        ModuleName(), "Install",
        [&]
        {
            // Cache the declarative keybind table once; settings are now explicit
            // per module so complex QML settings pages can own their view models.
            keybinds_ = Keybinds();

            if (auto* store = ctx.GetService<ISettingsStore>())
            {
                // Load module-specific settings; write defaults to disk on first run.
                IModuleSettings& ps = store->GetModuleSettings(SettingsSection().c_str());
                LoadSettings(ps);
                if (!ps.WasLoaded())
                {
                    SaveSettings(ps);
                    ps.Save();
                }

                // Load module keybinds from this module's own [<Module>.keybinds] section.
                IModuleSettings& kps = store->GetModuleSettings(KeybindsSection().c_str());
                LoadKeybinds(kps);
                const int keysBefore = kps.KeyCount();
                SaveKeybinds(kps);
                if (kps.KeyCount() > keysBefore)
                {
                    kps.Save();
                }
            }

            RegisterKeybinds(ctx);
            OnInstall(ctx);
        }
    );
}

void ModuleBase::BindHotkeys(Hotkeys& keys) noexcept
{
    framelift::Guard(
        ModuleName(), "BindHotkeys",
        [&]
        {
            OnBindHotkeys(keys);
        }
    );
}

void ModuleBase::Uninstall() noexcept
{
    framelift::Guard(
        ModuleName(), "Uninstall",
        [&]
        {
            OnUninstall();
        }
    );
}

void* ModuleBase::QueryInterface(const char* interfaceId) noexcept
{
    if (!interfaceId)
    {
        return nullptr;
    }
    if (std::strcmp(interfaceId, IHotkeyProvider::InterfaceId) == 0)
    {
        return static_cast<IHotkeyProvider*>(this);
    }
    if (std::strcmp(interfaceId, IEventHandler::InterfaceId) == 0)
    {
        return static_cast<IEventHandler*>(this);
    }
    if (std::strcmp(interfaceId, IMediaEventHandler::InterfaceId) == 0)
    {
        return static_cast<IMediaEventHandler*>(this);
    }
    if (std::strcmp(interfaceId, IShutdownHandler::InterfaceId) == 0)
    {
        return static_cast<IShutdownHandler*>(this);
    }
    return nullptr;
}

// Optional dispatch surfaces

bool ModuleBase::OnEvent(const AppEvent& e) noexcept
{
    return framelift::Guard(
        ModuleName(), "HandleEvent",
        [&]
        {
            return HandleEvent(e);
        }
    );
}

void ModuleBase::OnMediaEvent(const MediaEvent& e) noexcept
{
    framelift::Guard(
        ModuleName(), "HandleMediaEvent",
        [&]
        {
            HandleMediaEvent(e);
        }
    );
}

void ModuleBase::OnShutdown() noexcept
{
    framelift::Guard(
        ModuleName(), "HandleShutdown",
        [&]
        {
            HandleShutdown();
        }
    );
}

bool ModuleBase::HandleEvent(const AppEvent& e)
{
    // Route KeyDown to the key hook directly; OnEvent is already guarded.
    if (e.type == AppEventType::KeyDown)
    {
        return HandleKeyDownEvent(e);
    }
    return false;
}

// Table-driven hook defaults

void ModuleBase::LoadSettings(IModuleSettings& ps)
{
}

void ModuleBase::SaveSettings(IModuleSettings& ps)
{
}

void ModuleBase::LoadKeybinds(IModuleSettings& kps)
{
    for (const auto& kb : keybinds_)
    {
        *kb.storage = kps.GetString(kb.action, kb.def);
    }
}

void ModuleBase::SaveKeybinds(IModuleSettings& kps)
{
    for (const auto& kb : keybinds_)
    {
        kps.SetString(kb.action, kb.storage->c_str());
    }
}

void ModuleBase::RegisterKeybinds(IModuleContext& ctx)
{
    for (auto& kb : keybinds_)
    {
        framelift::RegisterKeybindEntry(ctx, kb.label, kb.action, *kb.storage, ModuleName(), kb.def);
    }
}

void ModuleBase::OnBindHotkeys(Hotkeys& keys)
{
    for (const auto& kb : keybinds_)
    {
        if (kb.onPress)
        {
            framelift::Bind(keys, kb.action, *kb.storage, kb.onPress);
        }
    }
}

void ModuleBase::PersistSettings()
{
    auto* store = ctx_ ? ctx_->GetService<ISettingsStore>() : nullptr;
    if (!store)
    {
        return;
    }
    IModuleSettings& ps = store->GetModuleSettings(SettingsSection().c_str());
    SaveSettings(ps);
    ps.Save();

    IModuleSettings& kps = store->GetModuleSettings(KeybindsSection().c_str());
    SaveKeybinds(kps);
    kps.Save();
}
