#pragma once

#include <QtCore/QObject>

class Console;

class ConsoleSettings final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString title READ Title CONSTANT)
    Q_PROPERTY(bool dirty READ Dirty NOTIFY changed)
    Q_PROPERTY(bool showDebug READ ShowDebug WRITE SetShowDebug NOTIFY changed)
    Q_PROPERTY(bool showInfo READ ShowInfo WRITE SetShowInfo NOTIFY changed)
    Q_PROPERTY(bool showWarn READ ShowWarn WRITE SetShowWarn NOTIFY changed)
    Q_PROPERTY(bool showError READ ShowError WRITE SetShowError NOTIFY changed)
    Q_PROPERTY(bool perfOnly READ PerfOnly WRITE SetPerfOnly NOTIFY changed)

public:
    explicit ConsoleSettings(Console& console);

    [[nodiscard]] QString Title() const;
    [[nodiscard]] bool Dirty() const;
    [[nodiscard]] bool ShowDebug() const;
    [[nodiscard]] bool ShowInfo() const;
    [[nodiscard]] bool ShowWarn() const;
    [[nodiscard]] bool ShowError() const;
    [[nodiscard]] bool PerfOnly() const;

    void SetShowDebug(bool value);
    void SetShowInfo(bool value);
    void SetShowWarn(bool value);
    void SetShowError(bool value);
    void SetPerfOnly(bool value);

    // Re-seed the draft from the console's live state when the page becomes visible,
    // so runtime changes (the console's Performance toggle, "logs perf on", filter
    // checkboxes) aren't overwritten by a stale draft on the next Save.
    Q_INVOKABLE void load();
    Q_INVOKABLE void save();
    Q_INVOKABLE void reset();

Q_SIGNALS:
    void changed();

private:
    void MarkDirty();

    Console& console_;
    bool dirty_ = false;
    bool showDebug_ = true;
    bool showInfo_ = true;
    bool showWarn_ = true;
    bool showError_ = true;
    bool perfOnly_ = false;
};
