#include "ConsoleSettings.h"
#include "Console.h"

ConsoleSettings::ConsoleSettings(Console& console)
    : console_(console), showDebug_(console.showDebug_), showInfo_(console.showInfo_), showWarn_(console.showWarn_),
      showError_(console.showError_), perfOnly_(console.perfOnly_)
{
}

QString ConsoleSettings::Title() const
{
    return QStringLiteral("Console");
}

bool ConsoleSettings::Dirty() const
{
    return dirty_;
}

bool ConsoleSettings::ShowDebug() const
{
    return showDebug_;
}

bool ConsoleSettings::ShowInfo() const
{
    return showInfo_;
}

bool ConsoleSettings::ShowWarn() const
{
    return showWarn_;
}

bool ConsoleSettings::ShowError() const
{
    return showError_;
}

bool ConsoleSettings::PerfOnly() const
{
    return perfOnly_;
}

void ConsoleSettings::SetShowDebug(bool value)
{
    if (showDebug_ != value)
    {
        showDebug_ = value;
        MarkDirty();
    }
}

void ConsoleSettings::SetShowInfo(bool value)
{
    if (showInfo_ != value)
    {
        showInfo_ = value;
        MarkDirty();
    }
}

void ConsoleSettings::SetShowWarn(bool value)
{
    if (showWarn_ != value)
    {
        showWarn_ = value;
        MarkDirty();
    }
}

void ConsoleSettings::SetShowError(bool value)
{
    if (showError_ != value)
    {
        showError_ = value;
        MarkDirty();
    }
}

void ConsoleSettings::SetPerfOnly(bool value)
{
    if (perfOnly_ != value)
    {
        perfOnly_ = value;
        MarkDirty();
    }
}

void ConsoleSettings::load()
{
    showDebug_ = console_.showDebug_;
    showInfo_ = console_.showInfo_;
    showWarn_ = console_.showWarn_;
    showError_ = console_.showError_;
    perfOnly_ = console_.perfOnly_;
    dirty_ = false;
    Q_EMIT changed();
}

void ConsoleSettings::save()
{
    console_.ApplySettings(showDebug_, showInfo_, showWarn_, showError_, perfOnly_);
    dirty_ = false;
    Q_EMIT changed();
}

void ConsoleSettings::reset()
{
    showDebug_ = true;
    showInfo_ = true;
    showWarn_ = true;
    showError_ = true;
    perfOnly_ = false;
    MarkDirty();
}

void ConsoleSettings::MarkDirty()
{
    dirty_ = true;
    Q_EMIT changed();
}
