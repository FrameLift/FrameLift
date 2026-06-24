#pragma once

#include <framelift/AppEvent.h>
#include <framelift/platform/IMediaPlayer.h>

class Hotkeys;
class IModuleContext;

// Minimal lifecycle interface for a runtime module.
//
// Modules opt into additional host dispatch surfaces by implementing the
// smaller interfaces below. This keeps simple modules light while ModuleBase can
// still provide the familiar convenience hooks for normal app modules.
class IModule
{
public:
    static constexpr const char* InterfaceId = "framelift.IModule";
    virtual ~IModule() = default;

    virtual void Install(IModuleContext& /*ctx*/) noexcept
    {
    }

    virtual void Uninstall() noexcept
    {
    }

    virtual void* QueryInterface(const char* /*interfaceId*/) noexcept
    {
        return nullptr;
    }
};

class IHotkeyProvider
{
public:
    static constexpr const char* InterfaceId = "framelift.IHotkeyProvider";
    virtual ~IHotkeyProvider() = default;

    virtual void BindHotkeys(Hotkeys& keys) noexcept = 0;
};

class IEventHandler
{
public:
    static constexpr const char* InterfaceId = "framelift.IEventHandler";
    virtual ~IEventHandler() = default;

    virtual bool OnEvent(const AppEvent& e) noexcept = 0;
};

class IMediaEventHandler
{
public:
    static constexpr const char* InterfaceId = "framelift.IMediaEventHandler";
    virtual ~IMediaEventHandler() = default;

    virtual void OnMediaEvent(const MediaEvent& e) noexcept = 0;
};

class IShutdownHandler
{
public:
    static constexpr const char* InterfaceId = "framelift.IShutdownHandler";
    virtual ~IShutdownHandler() = default;

    virtual void OnShutdown() noexcept = 0;
};
