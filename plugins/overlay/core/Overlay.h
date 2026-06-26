#pragma once

#include <framelift/core.h>
#include <framelift/platform.h>

#include <QtCore/QObject>
#include <memory>
#include <string>

// Combined idle-screen + HUD overlay. QML owns presentation; C++ exposes playback
// state, commands, and panel/settings insets.
class Overlay final : public QObject, public ModuleBase
{
    Q_OBJECT
    Q_PROPERTY(bool idle READ IsIdle NOTIFY playbackStateChanged)
    Q_PROPERTY(bool paused READ IsPaused NOTIFY playbackStateChanged)
    Q_PROPERTY(double position READ Position NOTIFY playbackPositionChanged)
    Q_PROPERTY(double duration READ Duration NOTIFY playbackPositionChanged)
    Q_PROPERTY(QString commandLabel READ CommandLabel NOTIFY commandShown)
    Q_PROPERTY(bool settingsOpen READ SettingsOpen NOTIFY layoutChanged)
    Q_PROPERTY(qreal leftInset READ LeftInset NOTIFY layoutChanged)
    Q_PROPERTY(qreal rightInset READ RightInset NOTIFY layoutChanged)

public:
    Overlay() = default;

    // Display `label` and reset the fade timer.
    void ShowCommand(std::string label);

    // True when the player has no file loaded (welcome screen showing).
    [[nodiscard]] bool IsIdle() const noexcept
    {
        return isIdle_;
    }

    [[nodiscard]] bool IsPaused() const noexcept
    {
        return isPaused_;
    }

    [[nodiscard]] double Position() const noexcept
    {
        return timePos_;
    }

    [[nodiscard]] double Duration() const noexcept
    {
        return duration_;
    }

    [[nodiscard]] QString CommandLabel() const
    {
        return QString::fromStdString(commandLabel_);
    }

    [[nodiscard]] bool SettingsOpen() const noexcept
    {
        return settingsOpen_;
    }

    [[nodiscard]] qreal LeftInset() const noexcept
    {
        return leftInset_;
    }

    [[nodiscard]] qreal RightInset() const noexcept
    {
        return rightInset_;
    }

    Q_INVOKABLE void togglePause();
    Q_INVOKABLE void seek(double seconds);

    bool HandleEvent(const AppEvent& e) override;
    void HandleMediaEvent(const MediaEvent& event) override;

protected:
    const char* ModuleName() const override
    {
        return "Overlay";
    }

    void OnInstall(IModuleContext& ctx) override;

Q_SIGNALS:
    void playbackStateChanged();
    void playbackPositionChanged();
    void commandShown();
    void layoutChanged();

private:
    IMediaPlayback* playback_ = nullptr; // transport (pause/seek), from ctx_ in OnInstall()
    IMediaProperties* props_ = nullptr;  // property observation, from ctx_ in OnInstall()

    bool isIdle_ = true; // true when no file is loaded (player is in idle state)
    bool isPaused_ = false;
    double timePos_ = 0.0;  // current playback position in seconds
    double duration_ = 0.0; // total duration of the current file in seconds

    // Cached panel insets / settings visibility, updated via events
    // (PanelLayoutEvent, SettingsVisibilityEvent).
    float leftInset_ = 0.f;
    float rightInset_ = 0.f;
    bool settingsOpen_ = false;

    std::string commandLabel_;
};

FRAMELIFT_MODULE_ENTRY(
    Overlay, {
                 .renderOrder = 0,
             }
)
