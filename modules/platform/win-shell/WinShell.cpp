#include "WinShell.h"
#include "TaskbarProgress.h"
#include "ToastNotifier.h"

#include <framelift/ContextHelpers.h>
#include <framelift/Events.h>
#include <framelift/IModuleSettings.h>
#include <framelift/services/ISettingsRegistry.h>
#include <framelift/services/ISettingsStore.h>
#include <framelift/ui/UIContext.h>

#include <filesystem>

namespace
{
constexpr const char* kSection = "winshell";
constexpr const char* kNotifyKey = "notifications";

std::string FilenameOf(const std::string& path)
{
    try
    {
        return std::filesystem::path(path).filename().string();
    }
    catch (...)
    {
        return path;
    }
}
} // namespace

WinShell::WinShell(void* hwnd)
    : taskbar_(std::make_unique<TaskbarProgress>(hwnd)), toast_(std::make_unique<ToastNotifier>())
{
    // No file is loaded yet — start idle so the taskbar stays clear until the
    // player reports otherwise (avoids a brief marquee before the first event).
    snap_.idle = true;
}

WinShell::~WinShell() = default;

void WinShell::Connect(IModuleContext& ctx)
{
    // Persisted enable/disable toggle in the [winshell] INI section (default on).
    store_ = ctx.GetService<ISettingsStore>();
    if (store_)
    {
        IModuleSettings& ps = store_->GetModuleSettings(kSection);
        notifyEnabled_ = ps.GetBool(kNotifyKey, true);
        // Seed the default to disk on first run so the section is hand-editable.
        if (!ps.WasLoaded())
        {
            ps.SetBool(kNotifyKey, notifyEnabled_);
            ps.Save();
        }
    }

    // Reflect the file currently playing in the error-toast text.
    framelift::Subscribe<FileOpenedEvent>(
        ctx,
        [this](const FileOpenedEvent& e)
        {
            currentFile_ = e.path ? e.path : "";
        }
    );

    // Push the properties the taskbar reflects.
    if (auto* props = ctx.GetService<IMediaProperties>())
    {
        props->ObserveProperty(PlayerProperty::IdleActive);
        props->ObserveProperty(PlayerProperty::TimePos);
        props->ObserveProperty(PlayerProperty::Duration);
        props->ObserveProperty(PlayerProperty::Pause);
        props->ObserveProperty(PlayerProperty::EofReached);
    }

    // Register the "Notifications" settings page (status + toggle + register button).
    if (auto* reg = ctx.GetService<ISettingsRegistry>())
    {
        reg->RegisterSettingsPage(
            "Notifications",
            [](void* ud, UIContext& c)
            {
                static_cast<WinShell*>(ud)->RenderSettings(c);
            },
            [](void* ud)
            {
                static_cast<WinShell*>(ud)->ApplySettings();
            },
            this, /*visible=*/true, /*cleanup=*/nullptr
        );
    }
}

void WinShell::OnMediaEvent(const MediaEvent& e)
{
    switch (e.type)
    {
    case MediaEventType::PropertyChange:
    {
        const auto& [prop, type, value] = e.property;
        switch (prop)
        {
        case PlayerProperty::IdleActive:
            if (type == PropertyType::Flag)
            {
                snap_.idle = value.flag != 0;
            }
            break;
        case PlayerProperty::TimePos:
            if (type == PropertyType::Double)
            {
                snap_.timePos = value.dbl >= 0.0 ? value.dbl : 0.0;
            }
            break;
        case PlayerProperty::Duration:
            if (type == PropertyType::Double)
            {
                snap_.duration = value.dbl > 0.0 ? value.dbl : 0.0;
            }
            break;
        case PlayerProperty::Pause:
            if (type == PropertyType::Flag)
            {
                snap_.paused = value.flag != 0;
            }
            break;
        case PlayerProperty::EofReached:
            if (type == PropertyType::Flag)
            {
                snap_.eof = value.flag != 0;
            }
            break;
        default:
            break;
        }
        break;
    }

    case MediaEventType::StartFile:
    case MediaEventType::FileLoaded:
        // A new file supersedes any prior error/EOF.
        snap_.errored = false;
        snap_.eof = false;
        snap_.idle = false;
        break;

    case MediaEventType::EndFile:
        if (e.endReason == EndFileReason::Error)
        {
            snap_.errored = true;
            if (notifyEnabled_ && toast_)
            {
                const auto now = std::chrono::steady_clock::now();
                const bool sameFile = hasLastError_ && currentFile_ == lastErrorFile_;
                const double sinceLast =
                    hasLastError_ ? std::chrono::duration<double>(now - lastErrorTime_).count() : 1.0e9;
                if (ShouldNotifyError(sameFile, sinceLast))
                {
                    toast_->NotifyError(FilenameOf(currentFile_).c_str());
                    lastErrorFile_ = currentFile_;
                    lastErrorTime_ = now;
                    hasLastError_ = true;
                }
            }
        }
        else
        {
            snap_.eof = true;
        }
        break;

    default:
        break;
    }

    PushIfChanged();
}

void WinShell::PushIfChanged()
{
    const int permille = ProgressPermille(snap_);
    const ProgressState state = MapState(snap_);
    if (!ProgressChanged(permille, state, lastPermille_, lastState_))
    {
        return;
    }
    if (taskbar_)
    {
        taskbar_->SetState(state);
        taskbar_->SetValue(permille);
    }
    lastPermille_ = permille;
    lastState_ = state;
}

void WinShell::OnShutdown()
{
    if (taskbar_)
    {
        taskbar_->Clear();
    }
}

void WinShell::RenderSettings(UIContext& ctx)
{
    ctx.Checkbox("Show playback-error notifications", &notifyEnabled_);

    ctx.Separator();

    if (toast_ && toast_->IsRegistered())
    {
        ctx.Text("Notifications are registered with Windows.");
        if (ctx.Button("Send test notification"))
        {
            toast_->Notify("FrameLift", "This is a test notification.");
        }
    }
    else
    {
        ctx.TextWrapped("Windows requires a one-time setup before it will show toast "
                        "notifications for this app.");
        if (ctx.Button("Enable notifications") && toast_)
        {
            (void)toast_->Register();
        }
    }
}

void WinShell::ApplySettings()
{
    if (store_)
    {
        IModuleSettings& ps = store_->GetModuleSettings(kSection);
        ps.SetBool(kNotifyKey, notifyEnabled_);
        ps.Save();
    }
}
