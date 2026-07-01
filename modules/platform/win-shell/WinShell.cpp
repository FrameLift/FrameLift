#include "WinShell.h"
#include "ToastNotifier.h"

#include <framelift/ContextHelpers.h>
#include <framelift/Events.h>
#include <framelift/IModuleSettings.h>
#include <framelift/services/ISettingsRegistry.h>
#include <framelift/services/ISettingsStore.h>

#include <filesystem>

namespace
{
constexpr const char* kSection = "winshell";
constexpr const char* kNotifyKey = "notifications";
constexpr double kErrorDebounceSeconds = 1.5;

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

bool ShouldNotifyError(bool sameFileAsLast, double secondsSinceLast) noexcept
{
    return !sameFileAsLast || secondsSinceLast >= kErrorDebounceSeconds;
}
} // namespace

WinShell::WinShell() : toast_(std::make_unique<ToastNotifier>()) {}

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

    // Register the settings toggle for the QML settings surface.
    if (auto* reg = ctx.GetService<ISettingsRegistry>())
    {
        const FrameLiftModuleSettingDesc desc{
            "winshell.notifications",
            0,
            "Show playback-error notifications.",
            "1",
            [](void* ud) -> const char*
            {
                auto* self = static_cast<WinShell*>(ud);
                self->notifyValue_ = self->notifyEnabled_ ? "1" : "0";
                return self->notifyValue_.c_str();
            },
            [](void* ud, const char* value)
            {
                auto* self = static_cast<WinShell*>(ud);
                self->notifyEnabled_ = value && (std::string(value) == "1" || std::string(value) == "true");
                self->SaveSettings();
            },
            this
        };
        reg->RegisterModuleSetting(&desc);
    }
}

void WinShell::OnMediaEvent(const MediaEvent& e)
{
    switch (e.type)
    {
    case MediaEventType::StartFile:
    case MediaEventType::FileLoaded:
        break;

    case MediaEventType::EndFile:
        if (e.endReason == EndFileReason::Error)
        {
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
        break;

    default:
        break;
    }
}

void WinShell::SaveSettings()
{
    if (store_)
    {
        IModuleSettings& ps = store_->GetModuleSettings(kSection);
        ps.SetBool(kNotifyKey, notifyEnabled_);
        ps.Save();
    }
}
