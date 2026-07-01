#include "LogViewer.h"
#include "LogViewerSettings.h"

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

void LogViewer::OnInstall(IModuleContext& ctx)
{
    logs_ = ctx.GetService<ILogBuffer>();
    if (auto* pages = ctx.GetService<ISettingsPageRegistry>())
    {
        settingsPage_ = std::make_unique<LogViewerSettings>(*this);
        pages->RegisterSettingsPage(
            "logViewer", "Log Viewer", "qrc:/qt/qml/FrameLift/Plugins/LogViewer/LogViewerSettings.qml",
            settingsPage_.get(), 320
        );
    }
    refreshTimer_ = new QTimer(this);
    refreshTimer_->setInterval(250);
    connect(
        refreshTimer_, &QTimer::timeout, this,
        [this]
        {
            if (Pull())
            {
                EmitLinesChanged();
            }
        }
    );
    // Timer is gated on open state (started in SetOpen), not running while hidden.
}

void LogViewer::SetOpen(const bool open)
{
    if (open_ == open)
    {
        return;
    }
    open_ = open;
    if (refreshTimer_)
    {
        if (open_)
        {
            Pull(); // drain any backlog immediately so the view isn't blank on show
            refreshTimer_->start();
        }
        else
        {
            refreshTimer_->stop();
        }
    }
    // Refresh the line projection first (drained backlog above), *then* flip the
    // panel's visibility — so the model is already populated when QML reveals it.
    EmitLinesChanged();
    Q_EMIT panelStateChanged();
}

void LogViewer::LoadSettings(IModuleSettings& ps)
{
    showDebug_ = ps.GetBool("showDebug", true);
    showInfo_ = ps.GetBool("showInfo", true);
    showWarn_ = ps.GetBool("showWarn", true);
    showError_ = ps.GetBool("showError", true);
    perfOnly_ = ps.GetBool("perfOnly", false);
}

void LogViewer::SaveSettings(IModuleSettings& ps)
{
    ps.SetBool("showDebug", showDebug_);
    ps.SetBool("showInfo", showInfo_);
    ps.SetBool("showWarn", showWarn_);
    ps.SetBool("showError", showError_);
    ps.SetBool("perfOnly", perfOnly_);
}

void LogViewer::ApplySettings(bool showDebug, bool showInfo, bool showWarn, bool showError, bool perfOnly)
{
    showDebug_ = showDebug;
    showInfo_ = showInfo;
    showWarn_ = showWarn;
    showError_ = showError;
    perfOnly_ = perfOnly;
    if (auto* store = ctx_ ? ctx_->GetService<ISettingsStore>() : nullptr)
    {
        IModuleSettings& ps = store->GetModuleSettings(SettingsSection().c_str());
        SaveSettings(ps);
        ps.Save();
    }
    EmitLinesChanged();
}

QVariantList LogViewer::QmlLines() const
{
    if (!linesCacheDirty_)
    {
        return linesCache_;
    }
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
    linesCache_ = std::move(result);
    linesCacheDirty_ = false;
    return linesCache_;
}

void LogViewer::clearLines()
{
    entries_.clear();
    EmitLinesChanged();
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

bool IsPerf(const int level)
{
    return static_cast<Log::Level>(level) == Log::Level::Perf;
}
} // namespace

bool LogViewer::Passes(const Entry& e) const
{
    if (perfOnly_ && !IsPerf(e.level))
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
    case Log::Level::Perf:
        break;
    }
    return ContainsNoCase(e.msg, filterText_);
}
