#pragma once

#include <framelift/services/ICommandRegistry.h>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

class CommandRegistry final : public ICommandRegistry
{
public:
    struct ParsedLine
    {
        std::vector<std::string> args;
        std::string error;
    };

    using HostHandler = std::function<void(const Invocation&)>;

    CommandRegistry() = default;
    CommandRegistry(const CommandRegistry&) = delete;
    CommandRegistry& operator=(const CommandRegistry&) = delete;
    ~CommandRegistry() override;

    [[nodiscard]] bool RegisterCommand(const CommandSpec* spec) noexcept override;
    bool RegisterHostCommand(
        std::string name, std::vector<std::string> aliases, std::string summary, std::string usage, HostHandler handler
    );
    void EnumerateCommands(Visitor visit, void* ud) const noexcept override;
    [[nodiscard]] bool Execute(const char* line, OutputFn output, void* outputUd) noexcept override;

    void ClearPluginCommands() noexcept;

    [[nodiscard]] static ParsedLine Tokenize(const std::string& line);
    [[nodiscard]] static bool ParseDurationSeconds(const std::string& token, double& seconds);

private:
    struct Command
    {
        std::string name;
        std::vector<std::string> aliases;
        std::string aliasesText;
        std::string summary;
        std::string usage;
        HandlerFn handler = nullptr;
        void* ud = nullptr;
        void (*cleanup)(void* ud) noexcept = nullptr;
        HostHandler hostHandler;
        bool pluginOwned = false;
    };

    [[nodiscard]] bool NameAvailable(const std::string& name) const;
    void IndexCommand(std::size_t index);
    void Cleanup(Command& command) noexcept;
    [[nodiscard]] static std::vector<std::string> SplitAliases(const char* aliases);
    static void Write(const Invocation& invocation, int level, const std::string& text) noexcept;

    std::vector<Command> commands_;
    std::unordered_map<std::string, std::size_t> lookup_;
};
