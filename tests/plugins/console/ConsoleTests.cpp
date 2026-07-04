#include "Console.h"
#include "ConsoleSettings.h" // complete type for Console's unique_ptr member

#include "CommandRegistry.h"
#include <framelift/IModuleContext.h>
#include <framelift/IModuleSettings.h>
#include <framelift/Log.h>
#include <framelift/services/ICommandRegistry.h>
#include <framelift/services/ILogBuffer.h>
#include <framelift/services/ISettingsStore.h>

#include "QtTestRunner.h"
#include <cstring>

#include <QtTest/QtTest>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace
{
// Minimal in-test ring: hands back entries with seq>after, oldest first.
class FakeLogBuffer final : public ILogBuffer
{
public:
    void Push(const int level, const char* msg)
    {
        entries_.emplace_back(entries_.size() + 1, 0LL, level, std::string(msg));
    }

    [[nodiscard]] unsigned long long LatestSeq() const noexcept override
    {
        return entries_.empty() ? 0 : std::get<0>(entries_.back());
    }

    unsigned long long ReadSince(const unsigned long long after, const Visitor v, void* ud) const noexcept override
    {
        unsigned long long last = after;
        for (const auto& e : entries_)
        {
            if (std::get<0>(e) <= after)
            {
                continue;
            }
            v(ud, std::get<0>(e), std::get<1>(e), std::get<2>(e), std::get<3>(e).c_str());
            last = std::get<0>(e);
        }
        return last;
    }

private:
    std::vector<std::tuple<unsigned long long, long long, int, std::string>> entries_;
};

class FakeContext final : public IModuleContext
{
public:
    ILogBuffer* logs = nullptr;
    ICommandRegistry* commands = nullptr;
    ISettingsStore* settings = nullptr;

    void* GetServiceRaw(const char* id) const noexcept override
    {
        if (logs && std::strcmp(id, ILogBuffer::InterfaceId) == 0)
        {
            return logs;
        }
        if (commands && std::strcmp(id, ICommandRegistry::InterfaceId) == 0)
        {
            return commands;
        }
        if (settings && std::strcmp(id, ISettingsStore::InterfaceId) == 0)
        {
            return settings;
        }
        return nullptr;
    }

    void RegisterServiceRaw(const char*, void*) noexcept override
    {
    }

    void SubscribeRaw(const char*, void (*)(const void*, void*), void*, void (*)(void*)) noexcept override
    {
    }

    void PublishRaw(const char*, const void*) noexcept override
    {
    }
};

class FakeModuleSettings final : public IModuleSettings
{
public:
    explicit FakeModuleSettings(bool loaded = false)
        : loaded_(loaded)
    {
    }

    void SetLoaded(bool loaded)
    {
        loaded_ = loaded;
    }

    [[nodiscard]] const char* GetString(const char* key, const char* def = "") const noexcept override
    {
        const auto it = values_.find(key ? key : "");
        return it == values_.end() ? def : it->second.c_str();
    }

    [[nodiscard]] int GetInt(const char* key, int def = 0) const noexcept override
    {
        const auto it = values_.find(key ? key : "");
        return it == values_.end() ? def : std::stoi(it->second);
    }

    [[nodiscard]] float GetFloat(const char* key, float def = 0.f) const noexcept override
    {
        const auto it = values_.find(key ? key : "");
        return it == values_.end() ? def : std::stof(it->second);
    }

    [[nodiscard]] bool GetBool(const char* key, bool def = false) const noexcept override
    {
        const auto it = values_.find(key ? key : "");
        return it == values_.end() ? def : it->second == "1";
    }

    void SetString(const char* key, const char* value) noexcept override
    {
        values_[key ? key : ""] = value ? value : "";
    }

    void SetInt(const char* key, int value) noexcept override
    {
        values_[key ? key : ""] = std::to_string(value);
    }

    void SetFloat(const char* key, float value) noexcept override
    {
        values_[key ? key : ""] = std::to_string(value);
    }

    void SetBool(const char* key, bool value) noexcept override
    {
        values_[key ? key : ""] = value ? "1" : "0";
    }

    void Save() noexcept override
    {
        loaded_ = true;
    }

    [[nodiscard]] bool WasLoaded() const noexcept override
    {
        return loaded_;
    }

    [[nodiscard]] int KeyCount() const noexcept override
    {
        return static_cast<int>(values_.size());
    }

private:
    std::unordered_map<std::string, std::string> values_;
    bool loaded_ = false;
};

class FakeSettingsStore final : public ISettingsStore
{
public:
    FakeModuleSettings& Section(const std::string& name, const bool loaded = true)
    {
        auto& section = sections_[name];
        if (!section)
        {
            section = std::make_unique<FakeModuleSettings>(loaded);
        }
        section->SetLoaded(loaded);
        return *section;
    }

    float GetSettingFloat(const char*) const noexcept override
    {
        return 0.f;
    }
    bool GetSettingBool(const char*) const noexcept override
    {
        return false;
    }
    int GetSettingInt(const char*) const noexcept override
    {
        return 0;
    }
    int GetSettingString(const char*, char*, int) const noexcept override
    {
        return 0;
    }
    void CommitSettingFloat(const char*, float) noexcept override
    {
    }
    void CommitSettingBool(const char*, bool) noexcept override
    {
    }
    void CommitSettingInt(const char*, int) noexcept override
    {
    }
    void CommitSettingString(const char*, const char*) noexcept override
    {
    }
    void SaveSettings() noexcept override
    {
    }
    void RegisterSettingsChangeCallback(void (*)(void*), void*, void (*)(void*)) noexcept override
    {
    }
    IModuleSettings& GetModuleSettings(const char* sectionName) noexcept override
    {
        const std::string name = sectionName ? sectionName : "";
        auto& section = sections_[name];
        if (!section)
        {
            section = std::make_unique<FakeModuleSettings>(false);
        }
        return *section;
    }
    int GetSettingsFilePath(char*, int) const noexcept override
    {
        return 0;
    }
    void ReloadSettings() noexcept override
    {
    }

private:
    std::unordered_map<std::string, std::unique_ptr<FakeModuleSettings>> sections_;
};
} // namespace

class ConsoleTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void ShowsBacklogOnOpen()
    {
        FakeLogBuffer buf;
        buf.Push(static_cast<int>(Log::Level::Info), "hello");
        buf.Push(static_cast<int>(Log::Level::Warn), "world");

        FakeContext ctx;
        ctx.logs = &buf;

        Console v;
        v.Install(ctx);

        QVERIFY(v.QmlLines().isEmpty()); // closed: nothing pulled yet
        v.Toggle();                      // open → must drain backlog immediately
        QVERIFY(v.IsOpen());
        QVERIFY((v.QmlLines().size()) == (2));
    }

    // Console is exercised without a host context: with no ILogBuffer wired up the
    // ring buffer stays empty, so these cover the context-free public surface
    // (visibility, filter state, line projection).

    void StartsClosed()
    {
        const Console v;
        QVERIFY(!(v.IsOpen()));
    }

    void ToggleAndCloseAreIdempotent()
    {
        Console v;
        v.Toggle();
        QVERIFY(v.IsOpen());
        v.close();
        QVERIFY(!(v.IsOpen()));
        v.close(); // closing an already-closed viewer is a no-op
        QVERIFY(!(v.IsOpen()));
    }

    void FilterTextRoundTrips()
    {
        Console v;
        v.SetFilterText(QStringLiteral("error"));
        QVERIFY((v.FilterText()) == (QStringLiteral("error")));
    }

    void PerfOnlyRoundTrips()
    {
        Console v;
        QVERIFY(!(v.PerfOnly()));
        v.SetPerfOnly(true);
        QVERIFY(v.PerfOnly());
    }

    void LinesEmptyWithoutLogBuffer()
    {
        Console v;
        QVERIFY(v.QmlLines().isEmpty());
        v.clearLines(); // safe with nothing buffered
        QVERIFY(v.QmlLines().isEmpty());
    }

    void SubmitCommandEchoesAndCapturesOutput()
    {
        CommandRegistry commands;
        FakeContext ctx;
        ctx.commands = &commands;

        Console v;
        v.Install(ctx);
        v.submitCommand(QStringLiteral("help"));

        const QVariantList lines = v.QmlLines();
        QVERIFY((lines.size()) >= (2));
        QVERIFY(lines[0].toMap().value(QStringLiteral("message")).toString().contains(QStringLiteral("> help")));
        QVERIFY(lines[1].toMap().value(QStringLiteral("message")).toString().contains(QStringLiteral("Commands")));
    }

    void LogsCommandUpdatesPerfFilter()
    {
        CommandRegistry commands;
        FakeContext ctx;
        ctx.commands = &commands;

        Console v;
        v.Install(ctx);
        v.submitCommand(QStringLiteral("logs perf on"));

        QVERIFY(v.PerfOnly());
    }

    void CommandHistoryNavigates()
    {
        CommandRegistry commands;
        FakeContext ctx;
        ctx.commands = &commands;

        Console v;
        v.Install(ctx);
        v.submitCommand(QStringLiteral("help"));

        QVERIFY((v.historyPrevious(QString())).compare(QStringLiteral("help")) == 0);
        QVERIFY(v.historyNext().isEmpty());
    }

    void SettingsPageLoadReseedsFromLiveState()
    {
        Console v;
        ConsoleSettings page(v);
        QVERIFY(!(page.PerfOnly()));

        // A runtime change to the console (its Performance toggle / "logs perf on")
        // after the page was constructed leaves the page's draft stale.
        v.SetPerfOnly(true);

        // load() must re-seed from the live console state, so a later Save doesn't
        // write the stale draft back and silently disable the perf filter.
        page.load();
        QVERIFY(page.PerfOnly());
    }

    void MigratesOldLogViewerSettings()
    {
        FakeSettingsStore store;
        FakeModuleSettings& old = store.Section("logViewer");
        old.SetBool("perfOnly", true);

        FakeContext ctx;
        ctx.settings = &store;

        Console v;
        v.Install(ctx);

        QVERIFY(v.PerfOnly());
        QVERIFY(store.GetModuleSettings("console").GetBool("perfOnly", false));
    }
};

namespace
{
const ::framelift::test::Registrar<ConsoleTest> kRegisterConsoleTest{"ConsoleTest"};
}

#include "ConsoleTests.moc"
