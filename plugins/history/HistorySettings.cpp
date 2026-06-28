#include "HistorySettings.h"
#include "History.h"

HistorySettings::HistorySettings(History& history) : history_(history)
{
    SeedFromHistory();
}

QString HistorySettings::Title() const
{
    return QStringLiteral("History");
}

bool HistorySettings::Dirty() const
{
    return dirty_;
}

int HistorySettings::MaxEntries() const
{
    return maxEntries_;
}

void HistorySettings::SetMaxEntries(int value)
{
    if (maxEntries_ != value)
    {
        maxEntries_ = value;
        dirty_ = true;
        Q_EMIT changed();
    }
}

void HistorySettings::save()
{
    history_.ApplySettings(maxEntries_);
    dirty_ = false;
    Q_EMIT changed();
}

void HistorySettings::reset()
{
    maxEntries_ = 200;
    dirty_ = true;
    Q_EMIT changed();
}

void HistorySettings::load()
{
    SeedFromHistory();
    Q_EMIT changed();
}

void HistorySettings::SeedFromHistory()
{
    maxEntries_ = history_.maxEntries_;
    dirty_ = false;
}
