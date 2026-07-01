#include "CommandRegistry.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <limits>

CommandRegistry::~CommandRegistry()
{
    for (auto& command : commands_)
    {
        Cleanup(command);
    }
}

bool CommandRegistry::RegisterCommand(const CommandSpec* spec) noexcept
{
    if (!spec || !spec->name || !spec->name[0] || !spec->handler)
    {
        return false;
    }

    Command command;
    command.name = spec->name;
    command.aliases = SplitAliases(spec->aliases);
    command.aliasesText = spec->aliases ? spec->aliases : "";
    command.summary = spec->summary ? spec->summary : "";
    command.usage = spec->usage ? spec->usage : spec->name;
    command.handler = spec->handler;
    command.ud = spec->ud;
    command.cleanup = spec->cleanup;
    command.pluginOwned = true;

    if (!NameAvailable(command.name))
    {
        Cleanup(command);
        return false;
    }
    for (const auto& alias : command.aliases)
    {
        if (!NameAvailable(alias))
        {
            Cleanup(command);
            return false;
        }
    }

    commands_.push_back(std::move(command));
    IndexCommand(commands_.size() - 1);
    return true;
}

bool CommandRegistry::RegisterHostCommand(
    std::string name, std::vector<std::string> aliases, std::string summary, std::string usage, HostHandler handler
)
{
    if (name.empty() || !handler || !NameAvailable(name))
    {
        return false;
    }
    for (const auto& alias : aliases)
    {
        if (alias.empty() || !NameAvailable(alias))
        {
            return false;
        }
    }

    Command command;
    command.name = std::move(name);
    command.aliases = std::move(aliases);
    for (const auto& alias : command.aliases)
    {
        if (!command.aliasesText.empty())
        {
            command.aliasesText.push_back(' ');
        }
        command.aliasesText += alias;
    }
    command.summary = std::move(summary);
    command.usage = std::move(usage);
    command.hostHandler = std::move(handler);
    commands_.push_back(std::move(command));
    IndexCommand(commands_.size() - 1);
    return true;
}

void CommandRegistry::EnumerateCommands(const Visitor visit, void* ud) const noexcept
{
    if (!visit)
    {
        return;
    }
    for (const auto& command : commands_)
    {
        visit(command.name.c_str(), command.aliasesText.c_str(), command.summary.c_str(), command.usage.c_str(), ud);
    }
}

bool CommandRegistry::Execute(const char* line, const OutputFn output, void* outputUd) noexcept
{
    Invocation invocation;
    invocation.output = output;
    invocation.outputUd = outputUd;

    const ParsedLine parsed = Tokenize(line ? line : "");
    if (!parsed.error.empty())
    {
        Write(invocation, OutputError, parsed.error);
        return false;
    }
    if (parsed.args.empty())
    {
        return true;
    }

    const auto it = lookup_.find(parsed.args.front());
    if (it == lookup_.end())
    {
        Write(invocation, OutputError, "Unknown command: " + parsed.args.front());
        Write(invocation, OutputInfo, "Try: help");
        return false;
    }

    std::vector<const char*> argv;
    argv.reserve(parsed.args.size());
    for (const auto& arg : parsed.args)
    {
        argv.push_back(arg.c_str());
    }
    invocation.argc = static_cast<int>(argv.size());
    invocation.argv = argv.data();

    try
    {
        Command& command = commands_[it->second];
        if (command.hostHandler)
        {
            command.hostHandler(invocation);
        }
        else if (command.handler)
        {
            command.handler(&invocation, command.ud);
        }
        return true;
    }
    catch (const std::exception& e)
    {
        Write(invocation, OutputError, std::string("Command failed: ") + e.what());
        return false;
    }
    catch (...)
    {
        Write(invocation, OutputError, "Command failed");
        return false;
    }
}

void CommandRegistry::ClearPluginCommands() noexcept
{
    lookup_.clear();
    std::vector<Command> kept;
    kept.reserve(commands_.size());
    for (auto& command : commands_)
    {
        if (command.pluginOwned)
        {
            Cleanup(command);
            continue;
        }
        kept.push_back(std::move(command));
    }
    commands_ = std::move(kept);
    for (std::size_t i = 0; i < commands_.size(); ++i)
    {
        IndexCommand(i);
    }
}

CommandRegistry::ParsedLine CommandRegistry::Tokenize(const std::string& line)
{
    ParsedLine result;
    std::string current;
    char quote = '\0';
    bool escaping = false;

    const auto flush = [&]
    {
        if (!current.empty())
        {
            result.args.push_back(std::move(current));
            current.clear();
        }
    };

    for (const char ch : line)
    {
        if (escaping)
        {
            current.push_back(ch);
            escaping = false;
            continue;
        }
        if (ch == '\\')
        {
            escaping = true;
            continue;
        }
        if (quote != '\0')
        {
            if (ch == quote)
            {
                quote = '\0';
            }
            else
            {
                current.push_back(ch);
            }
            continue;
        }
        if (ch == '"' || ch == '\'')
        {
            quote = ch;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(ch)))
        {
            flush();
            continue;
        }
        current.push_back(ch);
    }

    if (escaping)
    {
        current.push_back('\\');
    }
    if (quote != '\0')
    {
        result.error = "Unterminated quoted string";
        result.args.clear();
        return result;
    }
    flush();
    return result;
}

bool CommandRegistry::ParseDurationSeconds(const std::string& token, double& seconds)
{
    if (token.empty())
    {
        return false;
    }

    std::string number = token;
    double multiplier = 1.0;
    const auto setSuffix = [&](const char* suffix, const double value)
    {
        if (token.size() <= std::char_traits<char>::length(suffix))
        {
            return false;
        }
        if (token.ends_with(suffix))
        {
            number = token.substr(0, token.size() - std::char_traits<char>::length(suffix));
            multiplier = value;
            return true;
        }
        return false;
    };
    (void)(setSuffix("ms", 0.001) || setSuffix("s", 1.0) || setSuffix("m", 60.0) || setSuffix("h", 3600.0));

    double value = 0.0;
    const char* begin = number.data();
    const char* end = begin + number.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr != end || !std::isfinite(value))
    {
        return false;
    }
    seconds = value * multiplier;
    return true;
}

bool CommandRegistry::NameAvailable(const std::string& name) const
{
    return !name.empty() && !lookup_.contains(name);
}

void CommandRegistry::IndexCommand(const std::size_t index)
{
    lookup_[commands_[index].name] = index;
    for (const auto& alias : commands_[index].aliases)
    {
        lookup_[alias] = index;
    }
}

void CommandRegistry::Cleanup(Command& command) noexcept
{
    if (command.cleanup && command.ud)
    {
        command.cleanup(command.ud);
        command.cleanup = nullptr;
        command.ud = nullptr;
    }
}

std::vector<std::string> CommandRegistry::SplitAliases(const char* aliases)
{
    std::vector<std::string> result;
    std::string current;
    const auto flush = [&]
    {
        if (!current.empty())
        {
            result.push_back(std::move(current));
            current.clear();
        }
    };
    for (const char* p = aliases; p && *p; ++p)
    {
        const char ch = *p;
        if (std::isspace(static_cast<unsigned char>(ch)) || ch == ',' || ch == '|')
        {
            flush();
        }
        else
        {
            current.push_back(ch);
        }
    }
    flush();
    return result;
}

void CommandRegistry::Write(const Invocation& invocation, const int level, const std::string& text) noexcept
{
    if (invocation.output)
    {
        invocation.output(invocation.outputUd, level, text.c_str());
    }
}
