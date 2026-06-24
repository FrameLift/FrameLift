#pragma once
#include <framelift/Hotkeys.h>
#include <framelift/IModule.h>
#include <vector>

// Dynamic module registry. Add() installs the module immediately (services must
// be registered in ctx first). Optional runtime surfaces are dispatched only to
// modules that implement their small interfaces.
class ModuleRegistry
{
public:
    void Add(IModule* module, IModuleContext& ctx)
    {
        modules_.push_back(module);
        module->Install(ctx);
    }

    void Remove(IModule* module)
    {
        module->Uninstall();
        std::erase(modules_, module);
    }

    // Dispatch to each interested module until one consumes the event.
    bool OnEvent(const AppEvent& e)
    {
        for (auto* module : modules_)
        {
            auto* handler = static_cast<IEventHandler*>(module->QueryInterface(IEventHandler::InterfaceId));
            if (handler && handler->OnEvent(e))
            {
                return true;
            }
        }
        return false;
    }

    // Broadcast a media event to interested modules.
    void OnMediaEvent(const MediaEvent& e)
    {
        for (auto* module : modules_)
        {
            if (auto* handler = static_cast<IMediaEventHandler*>(module->QueryInterface(IMediaEventHandler::InterfaceId)))
            {
                handler->OnMediaEvent(e);
            }
        }
    }

    void BindHotkeys(Hotkeys& keys)
    {
        for (auto* module : modules_)
        {
            if (auto* provider = static_cast<IHotkeyProvider*>(module->QueryInterface(IHotkeyProvider::InterfaceId)))
            {
                provider->BindHotkeys(keys);
            }
        }
    }

    // Invoked after the main loop exits.
    void OnShutdown()
    {
        for (auto* module : modules_)
        {
            if (auto* handler = static_cast<IShutdownHandler*>(module->QueryInterface(IShutdownHandler::InterfaceId)))
            {
                handler->OnShutdown();
            }
        }
    }

private:
    std::vector<IModule*> modules_;
};
