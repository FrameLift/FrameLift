#include "Overlay.h"
#include <string>
#include <string_view>
#include <utility>

#include "Version.h"
#include <framelift/core.h>

namespace
{
Overlay* Instance = nullptr;
}

void Overlay::OnInstall(IModuleContext& ctx)
{
    Instance = this;

    // Both services are discovered and may be null; playback_ is null-checked at
    // each use site (TogglePause/SeekAbsolute), props_ is guarded here before its
    // immediate ObserveProperty calls.
    playback_ = ctx.GetService<IMediaPlayback>();
    props_ = ctx.GetService<IMediaProperties>();
    if (props_)
    {
        props_->ObserveProperty(PlayerProperty::IdleActive);
        props_->ObserveProperty(PlayerProperty::TimePos);
        props_->ObserveProperty(PlayerProperty::Duration);
        props_->ObserveProperty(PlayerProperty::Pause);
    }

    framelift::Subscribe<NotificationEvent>(
        ctx,
        [this](const NotificationEvent& e)
        {
            ShowCommand(e.text);
            // Keyboard seeks announce themselves as "Seek ..." notifications;
            // flash the floating seekbar for those too (slider seeks emit from seek()).
            if (std::string_view(e.text).starts_with("Seek"))
            {
                Q_EMIT seekTriggered();
            }
        }
    );

    framelift::Subscribe<PanelLayoutEvent>(
        ctx,
        [this](const PanelLayoutEvent& e)
        {
            (e.side == 0 ? leftInset_ : rightInset_) = e.visibleWidth;
            Q_EMIT layoutChanged();
        }
    );

    framelift::Subscribe<SettingsVisibilityEvent>(
        ctx,
        [this](const SettingsVisibilityEvent& e)
        {
            settingsOpen_ = e.open;
            Q_EMIT layoutChanged();
        }
    );
}

void Overlay::ShowCommand(std::string label)
{
    commandLabel_ = std::move(label);
    Q_EMIT commandShown();
}

void Overlay::togglePause()
{
    if (playback_)
    {
        playback_->TogglePause();
    }
}

void Overlay::seek(const double seconds)
{
    if (playback_)
    {
        playback_->SeekAbsolute(std::clamp(seconds, 0.0, duration_));
        Q_EMIT seekTriggered();
    }
}

bool Overlay::HandleEvent(const AppEvent& e)
{
    (void)e;
    return false;
}

void Overlay::HandleMediaEvent(const MediaEvent& event)
{
    if (event.type != MediaEventType::PropertyChange)
    {
        return;
    }

    const auto& [prop, type, value] = event.property;

    if (prop == PlayerProperty::IdleActive && type == PropertyType::Flag)
    {
        isIdle_ = value.flag != 0;
        Q_EMIT playbackStateChanged();
        return;
    }

    if (prop == PlayerProperty::Pause && type == PropertyType::Flag)
    {
        isPaused_ = value.flag != 0;
        Q_EMIT playbackStateChanged();
        return;
    }

    if (type != PropertyType::Double)
    {
        return;
    }

    const double val = value.dbl;
    if (prop == PlayerProperty::TimePos)
    {
        timePos_ = val >= 0.0 ? val : 0.0;
        Q_EMIT playbackPositionChanged();
    }
    if (prop == PlayerProperty::Duration)
    {
        duration_ = val > 0.0 ? val : 0.0;
        Q_EMIT playbackPositionChanged();
    }
}
