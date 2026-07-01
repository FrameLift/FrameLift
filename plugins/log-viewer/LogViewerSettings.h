#pragma once

#include <QtCore/QObject>

class LogViewer;

class LogViewerSettings final : public QObject
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
    explicit LogViewerSettings(LogViewer& logViewer);

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

    Q_INVOKABLE void save();
    Q_INVOKABLE void reset();

Q_SIGNALS:
    void changed();

private:
    void MarkDirty();

    LogViewer& logViewer_;
    bool dirty_ = false;
    bool showDebug_ = true;
    bool showInfo_ = true;
    bool showWarn_ = true;
    bool showError_ = true;
    bool perfOnly_ = false;
};
