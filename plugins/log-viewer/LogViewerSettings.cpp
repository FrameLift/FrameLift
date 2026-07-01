#include "LogViewerSettings.h"
#include "LogViewer.h"

LogViewerSettings::LogViewerSettings(LogViewer& logViewer)
    : logViewer_(logViewer), showDebug_(logViewer.showDebug_), showInfo_(logViewer.showInfo_),
      showWarn_(logViewer.showWarn_), showError_(logViewer.showError_), perfOnly_(logViewer.perfOnly_)
{
}

QString LogViewerSettings::Title() const
{
    return QStringLiteral("Log Viewer");
}

bool LogViewerSettings::Dirty() const
{
    return dirty_;
}

bool LogViewerSettings::ShowDebug() const
{
    return showDebug_;
}

bool LogViewerSettings::ShowInfo() const
{
    return showInfo_;
}

bool LogViewerSettings::ShowWarn() const
{
    return showWarn_;
}

bool LogViewerSettings::ShowError() const
{
    return showError_;
}

bool LogViewerSettings::PerfOnly() const
{
    return perfOnly_;
}

void LogViewerSettings::SetShowDebug(bool value)
{
    if (showDebug_ != value)
    {
        showDebug_ = value;
        MarkDirty();
    }
}

void LogViewerSettings::SetShowInfo(bool value)
{
    if (showInfo_ != value)
    {
        showInfo_ = value;
        MarkDirty();
    }
}

void LogViewerSettings::SetShowWarn(bool value)
{
    if (showWarn_ != value)
    {
        showWarn_ = value;
        MarkDirty();
    }
}

void LogViewerSettings::SetShowError(bool value)
{
    if (showError_ != value)
    {
        showError_ = value;
        MarkDirty();
    }
}

void LogViewerSettings::SetPerfOnly(bool value)
{
    if (perfOnly_ != value)
    {
        perfOnly_ = value;
        MarkDirty();
    }
}

void LogViewerSettings::save()
{
    logViewer_.ApplySettings(showDebug_, showInfo_, showWarn_, showError_, perfOnly_);
    dirty_ = false;
    Q_EMIT changed();
}

void LogViewerSettings::reset()
{
    showDebug_ = true;
    showInfo_ = true;
    showWarn_ = true;
    showError_ = true;
    perfOnly_ = false;
    MarkDirty();
}

void LogViewerSettings::MarkDirty()
{
    dirty_ = true;
    Q_EMIT changed();
}
