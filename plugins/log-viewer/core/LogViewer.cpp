#include "LogViewer.h"

#include <framelift/Log.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>

// ── ModuleBase hooks ────────────────────────────────────────────────────────

std::vector<framelift::Keybind> LogViewer::Keybinds()
{
    return {
        {"Toggle log viewer", "toggleLogViewer", &toggleKey_, "Ctrl+L", [this] { Toggle(); }}
    };
}

std::vector<framelift::SettingsField> LogViewer::SettingsFields()
{
    return {
        {"showDebug", &showDebug_, true},
        {"showInfo", &showInfo_, true},
        {"showWarn", &showWarn_, true},
        {"showError", &showError_, true},
        {"perfOnly", &perfOnly_, false},
    };
}

void LogViewer::OnInstall(IModuleContext& ctx)
{
    logs_ = ctx.GetService<ILogBuffer>();
    SetupSettingsPage(ctx, false);
}

// ── Log pull ────────────────────────────────────────────────────────────────

void LogViewer::OnEntry(void* ud, const unsigned long long seq, const long long tsMillis, const int level,
                        const char* msg)
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

// ── Filtering ─────────────────────────────────────────────────────────────────

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
        { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); }
    );
    return it != hay.end();
}

bool IsPerf(const std::string& msg)
{
    return msg.rfind("[perf]", 0) == 0;
}

const char* LevelName(const int level)
{
    switch (static_cast<Log::Level>(level))
    {
    case Log::Level::Debug:
        return "DEBUG";
    case Log::Level::Info:
        return "INFO ";
    case Log::Level::Warn:
        return "WARN ";
    case Log::Level::Error:
        return "ERROR";
    }
    return "?????";
}

UI::Color4f LevelColor(const int level)
{
    switch (static_cast<Log::Level>(level))
    {
    case Log::Level::Debug:
        return {0.55f, 0.55f, 0.55f, 1.f};
    case Log::Level::Info:
        return {0.85f, 0.85f, 0.85f, 1.f};
    case Log::Level::Warn:
        return {1.f, 0.8f, 0.3f, 1.f};
    case Log::Level::Error:
        return {1.f, 0.4f, 0.4f, 1.f};
    }
    return {1.f, 1.f, 1.f, 1.f};
}

std::string FormatStamp(const long long tsMillis)
{
    const std::time_t secs = static_cast<std::time_t>(tsMillis / 1000);
    const int ms = static_cast<int>(tsMillis % 1000);
    // Render thread only — std::localtime's shared buffer is not a concern here.
    const std::tm* tm = std::localtime(&secs);
    char buf[32];
    if (tm)
    {
        std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d", tm->tm_hour, tm->tm_min, tm->tm_sec, ms);
    }
    else
    {
        std::snprintf(buf, sizeof(buf), "??:??:??.%03d", ms);
    }
    return buf;
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

// ── Render ────────────────────────────────────────────────────────────────────

void LogViewer::OnRender(UIContext& ctx)
{
    if (!open_)
    {
        return;
    }

    // Live tail: keep painting only while new lines are actually arriving from the async
    // ring buffer (it posts no wake event of its own). An open-but-quiet log view requests
    // no redraw, so the demand-driven loop sleeps instead of spinning.
    if (Pull())
    {
        ctx.RequestRedraw();
    }

    ctx.SetNextWindowSize({760.f, 420.f}, UI::Cond::FirstUseEver);
    if (!ctx.Begin("Log Viewer", &open_, UI::WindowFlags::None))
    {
        ctx.End();
        return;
    }

    if (!logs_)
    {
        ctx.TextColored({1.f, 0.4f, 0.4f, 1.f}, "Log buffer service unavailable.");
        ctx.End();
        return;
    }

    // ── Controls ────────────────────────────────────────────────────────────
    ctx.Checkbox("Debug", &showDebug_);
    ctx.SameLine();
    ctx.Checkbox("Info", &showInfo_);
    ctx.SameLine();
    ctx.Checkbox("Warn", &showWarn_);
    ctx.SameLine();
    ctx.Checkbox("Error", &showError_);
    ctx.SameLine();
    ctx.Checkbox("Perf only", &perfOnly_);
    ctx.SameLine();
    if (ctx.Button("Clear"))
    {
        entries_.clear();
    }

    ctx.SetNextItemWidth(-1.f);
    ctx.InputTextWithHint("##logfilter", "Filter...", filterText_);

    ctx.Separator();

    // ── Scrolling log ─────────────────────────────────────────────────────────
    if (ctx.BeginChild("##loglines", {0.f, 0.f}))
    {
        char prefix[48];
        for (const Entry& e : entries_)
        {
            if (!Passes(e))
            {
                continue;
            }
            std::snprintf(prefix, sizeof(prefix), "%s  %s  ", FormatStamp(e.tsMillis).c_str(), LevelName(e.level));
            ctx.TextDisabled(prefix);
            ctx.SameLine();
            ctx.TextColored(LevelColor(e.level), e.msg.c_str());
        }
    }
    ctx.EndChild();

    ctx.End();
}
