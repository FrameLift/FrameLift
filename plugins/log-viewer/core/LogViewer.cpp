#include "LogViewer.h"

#include <framelift/Log.h>

#include <QtCore/QTimer>
#include <QtCore/QVariantMap>
#include <algorithm>
#include <cctype>

std::vector<framelift::Keybind> LogViewer::Keybinds()
{
    return {
        {"Toggle log viewer", "toggleLogViewer", &toggleKey_, "Ctrl+L", [this]
         {
             Toggle();
         }}
    };
}

std::vector<framelift::SettingsField> LogViewer::SettingsFields()
{
    return {
        {"showDebug", &showDebug_, true}, {"showInfo", &showInfo_, true},  {"showWarn", &showWarn_, true},
        {"showError", &showError_, true}, {"perfOnly", &perfOnly_, false},
    };
}

void LogViewer::OnInstall(IModuleContext& ctx)
{
    logs_ = ctx.GetService<ILogBuffer>();
    refreshTimer_ = new QTimer(this);
    refreshTimer_->setInterval(250);
    connect(
        refreshTimer_, &QTimer::timeout, this,
        [this]
        {
            if (open_ && Pull())
            {
                Q_EMIT changed();
            }
        }
    );
    refreshTimer_->start();
}

QVariantList LogViewer::QmlLines() const
{
    QVariantList result;
    for (const Entry& entry : entries_)
    {
        if (!Passes(entry))
        {
            continue;
        }
        QVariantMap row;
        row.insert(QStringLiteral("message"), QString::fromStdString(entry.msg));
        row.insert(QStringLiteral("level"), entry.level);
        row.insert(QStringLiteral("timestamp"), entry.tsMillis);
        result.push_back(row);
    }
    return result;
}

void LogViewer::clearLines()
{
    entries_.clear();
    Q_EMIT changed();
}

void LogViewer::OnEntry(
    void* ud, const unsigned long long seq, const long long tsMillis, const int level, const char* msg
)
{
    auto* self = static_cast<LogViewer*>(ud);
    self->entries_.push_back({seq, tsMillis, level, msg ? msg : ""});
    while (self->entries_.size() > kMaxEntries)
    {
        self->entries_.pop_front();
    }
}

bool LogViewer::Pull()
{
    if (!logs_)
    {
        return false;
    }
    const unsigned long long before = lastSeq_;
    lastSeq_ = logs_->ReadSince(lastSeq_, &OnEntry, this);
    return lastSeq_ != before;
}

namespace
{
bool ContainsNoCase(const std::string& hay, const std::string& needle)
{
    if (needle.empty())
    {
        return true;
    }
    const auto it = std::search(
        hay.begin(), hay.end(), needle.begin(), needle.end(),
        [](const char a, const char b)
        {
            return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
        }
    );
    return it != hay.end();
}

bool IsPerf(const std::string& msg)
{
    return msg.rfind("[perf]", 0) == 0;
}
} // namespace

bool LogViewer::Passes(const Entry& e) const
{
    if (perfOnly_ && !IsPerf(e.msg))
    {
        return false;
    }
    switch (static_cast<Log::Level>(e.level))
    {
    case Log::Level::Debug:
        if (!showDebug_)
        {
            return false;
        }
        break;
    case Log::Level::Info:
        if (!showInfo_)
        {
            return false;
        }
        break;
    case Log::Level::Warn:
        if (!showWarn_)
        {
            return false;
        }
        break;
    case Log::Level::Error:
        if (!showError_)
        {
            return false;
        }
        break;
    }
    return ContainsNoCase(e.msg, filterText_);
}
