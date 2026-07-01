#include "Console.h"
#include "ConsoleSettings.h"

#include <framelift/Log.h>

#include <QtCore/QDateTime>
#include <QtCore/QTimer>
#include <QtCore/QVariantMap>
#include <algorithm>
#include <cctype>
#include <ranges>
#include <sstream>

std::vector<framelift::Keybind> Console::Keybinds()
{
    return {
        {"Toggle console", "toggleConsole", &toggleKey_, "Ctrl+L", [this]
         {
             Toggle();
         }}
    };
}

void Console::OnInstall(IModuleContext& ctx)
{
    logs_ = ctx.GetService<ILogBuffer>();
    commands_ = ctx.GetService<ICommandRegistry>();
    RegisterLocalCommands(ctx);
    if (auto* pages = ctx.GetService<ISettingsPageRegistry>())
    {
        settingsPage_ = std::make_unique<ConsoleSettings>(*this);
        pages->RegisterSettingsPage(
            "console", "Console", "qrc:/qt/qml/FrameLift/Plugins/Console/ConsoleSettings.qml", settingsPage_.get(), 320
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

void Console::SetOpen(const bool open)
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

void Console::LoadSettings(IModuleSettings& ps)
{
    IModuleSettings* source = &ps;
    if (!ps.WasLoaded())
    {
        if (auto* store = ctx_ ? ctx_->GetService<ISettingsStore>() : nullptr)
        {
            IModuleSettings& old = store->GetModuleSettings("logViewer");
            if (old.WasLoaded())
            {
                source = &old;
            }
        }
    }
    showDebug_ = source->GetBool("showDebug", true);
    showInfo_ = source->GetBool("showInfo", true);
    showWarn_ = source->GetBool("showWarn", true);
    showError_ = source->GetBool("showError", true);
    perfOnly_ = source->GetBool("perfOnly", false);

    commandHistory_.clear();
    std::istringstream in(source->GetString("commandHistory", ""));
    std::string line;
    while (std::getline(in, line))
    {
        if (!line.empty())
        {
            commandHistory_.push_back(line);
        }
    }
    while (commandHistory_.size() > kMaxCommandHistory)
    {
        commandHistory_.erase(commandHistory_.begin());
    }
    historyCursor_ = static_cast<int>(commandHistory_.size());
}

void Console::SaveSettings(IModuleSettings& ps)
{
    ps.SetBool("showDebug", showDebug_);
    ps.SetBool("showInfo", showInfo_);
    ps.SetBool("showWarn", showWarn_);
    ps.SetBool("showError", showError_);
    ps.SetBool("perfOnly", perfOnly_);

    std::string history;
    for (const auto& command : commandHistory_)
    {
        history += command;
        history.push_back('\n');
    }
    ps.SetString("commandHistory", history.c_str());
}

void Console::LoadKeybinds(IModuleSettings& kps)
{
    if (kps.WasLoaded())
    {
        ModuleBase::LoadKeybinds(kps);
        return;
    }
    if (auto* store = ctx_ ? ctx_->GetService<ISettingsStore>() : nullptr)
    {
        IModuleSettings& old = store->GetModuleSettings("logViewer.keybinds");
        if (old.WasLoaded())
        {
            toggleKey_ = old.GetString("toggleLogViewer", toggleKey_.c_str());
            return;
        }
    }
    ModuleBase::LoadKeybinds(kps);
}

void Console::ApplySettings(bool showDebug, bool showInfo, bool showWarn, bool showError, bool perfOnly)
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

QVariantList Console::QmlLines() const
{
    if (!linesCacheDirty_)
    {
        return linesCache_;
    }
    QVariantList result;
    std::vector<const Entry*> visible;
    visible.reserve(entries_.size() + consoleEntries_.size());
    for (const Entry& entry : entries_)
    {
        if (Passes(entry))
        {
            visible.push_back(&entry);
        }
    }
    for (const Entry& entry : consoleEntries_)
    {
        visible.push_back(&entry);
    }
    std::ranges::sort(
        visible,
        [](const Entry* a, const Entry* b)
        {
            if (a->tsMillis != b->tsMillis)
            {
                return a->tsMillis < b->tsMillis;
            }
            return static_cast<int>(a->kind) < static_cast<int>(b->kind);
        }
    );
    for (const Entry* entry : visible)
    {
        QVariantMap row;
        row.insert(
            QStringLiteral("kind"),
            entry->kind == Entry::Kind::Log
                ? QStringLiteral("log")
                : (entry->kind == Entry::Kind::Input ? QStringLiteral("input") : QStringLiteral("output"))
        );
        row.insert(QStringLiteral("message"), QString::fromStdString(entry->msg));
        row.insert(QStringLiteral("level"), entry->level);
        row.insert(QStringLiteral("timestamp"), entry->tsMillis);
        result.push_back(row);
    }
    linesCache_ = std::move(result);
    linesCacheDirty_ = false;
    return linesCache_;
}

void Console::clearLines()
{
    entries_.clear();
    consoleEntries_.clear();
    EmitLinesChanged();
}

void Console::submitCommand(const QString& command)
{
    const std::string text = command.trimmed().toStdString();
    if (text.empty())
    {
        return;
    }

    AppendConsoleLine(Entry::Kind::Input, ICommandRegistry::OutputInfo, "> " + text);
    if (commandHistory_.empty() || commandHistory_.back() != text)
    {
        commandHistory_.push_back(text);
        while (commandHistory_.size() > kMaxCommandHistory)
        {
            commandHistory_.erase(commandHistory_.begin());
        }
    }
    historyCursor_ = static_cast<int>(commandHistory_.size());
    historyDraft_.clear();

    if (!commands_)
    {
        AppendConsoleLine(Entry::Kind::Output, ICommandRegistry::OutputError, "Command service unavailable");
        return;
    }
    commands_->Execute(text.c_str(), &Console::OnCommandOutput, this);

    if (auto* store = ctx_ ? ctx_->GetService<ISettingsStore>() : nullptr)
    {
        IModuleSettings& ps = store->GetModuleSettings(SettingsSection().c_str());
        SaveSettings(ps);
        ps.Save();
    }
}

QString Console::historyPrevious(const QString& current)
{
    if (commandHistory_.empty())
    {
        return current;
    }
    if (historyCursor_ == static_cast<int>(commandHistory_.size()))
    {
        historyDraft_ = current.toStdString();
    }
    if (historyCursor_ > 0)
    {
        --historyCursor_;
    }
    return QString::fromStdString(commandHistory_[static_cast<std::size_t>(historyCursor_)]);
}

QString Console::historyNext()
{
    if (commandHistory_.empty())
    {
        return {};
    }
    if (historyCursor_ < static_cast<int>(commandHistory_.size()))
    {
        ++historyCursor_;
    }
    if (historyCursor_ == static_cast<int>(commandHistory_.size()))
    {
        return QString::fromStdString(historyDraft_);
    }
    return QString::fromStdString(commandHistory_[static_cast<std::size_t>(historyCursor_)]);
}

void Console::OnEntry(
    void* ud, const unsigned long long seq, const long long tsMillis, const int level, const char* msg
)
{
    auto* self = static_cast<Console*>(ud);
    self->entries_.push_back({Entry::Kind::Log, seq, tsMillis, level, msg ? msg : ""});
    while (self->entries_.size() > kMaxEntries)
    {
        self->entries_.pop_front();
    }
}

bool Console::Pull()
{
    if (!logs_)
    {
        return false;
    }
    const unsigned long long before = lastSeq_;
    lastSeq_ = logs_->ReadSince(lastSeq_, &OnEntry, this);
    return lastSeq_ != before;
}

void Console::AppendConsoleLine(const Entry::Kind kind, const int level, std::string msg)
{
    consoleEntries_.push_back({kind, 0, QDateTime::currentMSecsSinceEpoch(), level, std::move(msg)});
    while (consoleEntries_.size() > kMaxEntries)
    {
        consoleEntries_.pop_front();
    }
    EmitLinesChanged();
}

void Console::RegisterLocalCommands(IModuleContext& ctx)
{
    auto* registry = ctx.GetService<ICommandRegistry>();
    if (!registry)
    {
        return;
    }

    ICommandRegistry::CommandSpec help;
    help.name = "help";
    help.summary = "List commands";
    help.usage = "help";
    help.handler = &Console::HelpCommand;
    help.ud = this;
    registry->RegisterCommand(&help);

    ICommandRegistry::CommandSpec clear;
    clear.name = "clear";
    clear.summary = "Clear console scrollback";
    clear.usage = "clear";
    clear.handler = &Console::ClearCommand;
    clear.ud = this;
    registry->RegisterCommand(&clear);

    ICommandRegistry::CommandSpec logs;
    logs.name = "logs";
    logs.summary = "Manage log output";
    logs.usage = "logs clear|filter <text>|perf on|off|toggle";
    logs.handler = &Console::LogsCommand;
    logs.ud = this;
    registry->RegisterCommand(&logs);
}

void Console::OnCommandOutput(void* ud, const int level, const char* text) noexcept
{
    auto* self = static_cast<Console*>(ud);
    self->AppendConsoleLine(Entry::Kind::Output, level, text ? text : "");
}

void Console::HelpCommand(const ICommandRegistry::Invocation* invocation, void* ud) noexcept
{
    if (!invocation || !invocation->output)
    {
        return;
    }
    auto* self = static_cast<Console*>(ud);
    invocation->output(invocation->outputUd, ICommandRegistry::OutputInfo, "Commands:");
    if (!self->commands_)
    {
        return;
    }
    self->commands_->EnumerateCommands(
        [](const char* name, const char*, const char* summary, const char* usage, void* visitUd) noexcept
        {
            auto* invocation = static_cast<const ICommandRegistry::Invocation*>(visitUd);
            std::string line = name ? name : "";
            if (summary && summary[0])
            {
                line += " - ";
                line += summary;
            }
            if (usage && usage[0])
            {
                line += " (";
                line += usage;
                line += ")";
            }
            invocation->output(invocation->outputUd, ICommandRegistry::OutputInfo, line.c_str());
        },
        const_cast<ICommandRegistry::Invocation*>(invocation)
    );
}

void Console::ClearCommand(const ICommandRegistry::Invocation* invocation, void* ud) noexcept
{
    auto* self = static_cast<Console*>(ud);
    self->clearLines();
    if (invocation && invocation->output)
    {
        invocation->output(invocation->outputUd, ICommandRegistry::OutputInfo, "Cleared");
    }
}

void Console::LogsCommand(const ICommandRegistry::Invocation* invocation, void* ud) noexcept
{
    auto* self = static_cast<Console*>(ud);
    if (!invocation || invocation->argc < 2)
    {
        if (invocation && invocation->output)
        {
            invocation->output(
                invocation->outputUd, ICommandRegistry::OutputError,
                "Usage: logs clear|filter <text>|perf on|off|toggle"
            );
        }
        return;
    }
    const std::string sub = invocation->argv[1];
    if (sub == "clear" && invocation->argc == 2)
    {
        self->entries_.clear();
        self->EmitLinesChanged();
        invocation->output(invocation->outputUd, ICommandRegistry::OutputInfo, "Logs cleared");
        return;
    }
    if (sub == "filter")
    {
        std::string filter;
        for (int i = 2; i < invocation->argc; ++i)
        {
            if (!filter.empty())
            {
                filter.push_back(' ');
            }
            filter += invocation->argv[i];
        }
        self->SetFilterText(QString::fromStdString(filter));
        invocation->output(invocation->outputUd, ICommandRegistry::OutputInfo, "Log filter updated");
        return;
    }
    if (sub == "perf" && invocation->argc == 3)
    {
        const std::string value = invocation->argv[2];
        if (value == "on")
        {
            self->SetPerfOnly(true);
        }
        else if (value == "off")
        {
            self->SetPerfOnly(false);
        }
        else if (value == "toggle")
        {
            self->SetPerfOnly(!self->PerfOnly());
        }
        else
        {
            invocation->output(
                invocation->outputUd, ICommandRegistry::OutputError,
                "Usage: logs clear|filter <text>|perf on|off|toggle"
            );
            return;
        }
        invocation->output(invocation->outputUd, ICommandRegistry::OutputInfo, "Performance filter updated");
        return;
    }
    invocation->output(
        invocation->outputUd, ICommandRegistry::OutputError, "Usage: logs clear|filter <text>|perf on|off|toggle"
    );
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

bool Console::Passes(const Entry& e) const
{
    if (e.kind != Entry::Kind::Log)
    {
        return true;
    }
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
