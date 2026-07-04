#include "CoreCommands.h"

#include "CommandRegistry.h"
#include "FFmpegPlayer.h"
#include "ModuleContext.h"
#include <framelift/Events.h>
#include <framelift/platform/IAppWindow.h>

#include <charconv>
#include <sstream>
#include <string>
#include <utility>

namespace
{
void CommandWrite(const ICommandRegistry::Invocation& invocation, const int level, const std::string& text)
{
    if (invocation.output)
    {
        invocation.output(invocation.outputUd, level, text.c_str());
    }
}

void CommandInfo(const ICommandRegistry::Invocation& invocation, const std::string& text)
{
    CommandWrite(invocation, ICommandRegistry::OutputInfo, text);
}

void CommandError(const ICommandRegistry::Invocation& invocation, const std::string& text)
{
    CommandWrite(invocation, ICommandRegistry::OutputError, text);
}

void CommandUsage(const ICommandRegistry::Invocation& invocation, const std::string& usage)
{
    CommandError(invocation, "Usage: " + usage);
}

bool ParseInt(const char* text, int& value)
{
    if (!text || !text[0])
    {
        return false;
    }
    const char* end = text + std::char_traits<char>::length(text);
    // std::from_chars rejects a leading '+' (it accepts only '-'), which would
    // silently break the documented "volume +N" relative-adjust syntax. Skip it.
    const char* begin = text[0] == '+' ? text + 1 : text;
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    return ec == std::errc{} && ptr == end;
}

std::string BufferString(const auto& read)
{
    const int len = read(nullptr, 0);
    if (len <= 0)
    {
        return {};
    }
    std::string out(static_cast<std::size_t>(len), '\0');
    read(out.data(), len + 1);
    return out;
}
} // namespace

namespace host
{
void RegisterCoreCommands(
    CommandRegistry& commands, ModuleContext& context, FFmpegPlayer& player, IAppWindow& window,
    const std::string& pluginsPath
)
{
    commands.RegisterHostCommand(
        "open-file", {"open"}, "Open a media file", "open-file <path>",
        [&context](const ICommandRegistry::Invocation& invocation)
        {
            if (invocation.argc != 2)
            {
                CommandUsage(invocation, "open-file <path>");
                return;
            }
            context.Publish(OpenFileRequestEvent{invocation.argv[1], true});
            CommandInfo(invocation, std::string("Opening ") + invocation.argv[1]);
        }
    );

    commands.RegisterHostCommand(
        "pause", {}, "Pause playback", "pause",
        [&player](const ICommandRegistry::Invocation& invocation)
        {
            if (invocation.argc != 1)
            {
                CommandUsage(invocation, "pause");
                return;
            }
            player.SetPause(true);
            CommandInfo(invocation, "Paused");
        }
    );
    commands.RegisterHostCommand(
        "play", {}, "Resume playback", "play",
        [&player](const ICommandRegistry::Invocation& invocation)
        {
            if (invocation.argc != 1)
            {
                CommandUsage(invocation, "play");
                return;
            }
            player.SetPause(false);
            CommandInfo(invocation, "Playing");
        }
    );
    commands.RegisterHostCommand(
        "toggle-pause", {"pause-toggle"}, "Toggle pause", "toggle-pause",
        [&player](const ICommandRegistry::Invocation& invocation)
        {
            if (invocation.argc != 1)
            {
                CommandUsage(invocation, "toggle-pause");
                return;
            }
            player.TogglePause();
            CommandInfo(invocation, "Toggled pause");
        }
    );

    commands.RegisterHostCommand(
        "seek", {}, "Seek relative to the current position", "seek <duration>",
        [&player](const ICommandRegistry::Invocation& invocation)
        {
            double seconds = 0.0;
            if (invocation.argc != 2 || !CommandRegistry::ParseDurationSeconds(invocation.argv[1], seconds))
            {
                CommandUsage(invocation, "seek <duration>");
                return;
            }
            player.Seek(seconds);
            CommandInfo(invocation, std::string("Seeked ") + invocation.argv[1]);
        }
    );
    commands.RegisterHostCommand(
        "seek-to", {}, "Seek to an absolute position", "seek-to <duration>",
        [&player](const ICommandRegistry::Invocation& invocation)
        {
            double seconds = 0.0;
            if (invocation.argc != 2 || !CommandRegistry::ParseDurationSeconds(invocation.argv[1], seconds))
            {
                CommandUsage(invocation, "seek-to <duration>");
                return;
            }
            player.SeekAbsolute(seconds);
            CommandInfo(invocation, std::string("Seeked to ") + invocation.argv[1]);
        }
    );

    commands.RegisterHostCommand(
        "mute", {}, "Toggle mute", "mute",
        [&player](const ICommandRegistry::Invocation& invocation)
        {
            if (invocation.argc != 1)
            {
                CommandUsage(invocation, "mute");
                return;
            }
            player.ToggleMute();
            CommandInfo(invocation, "Toggled mute");
        }
    );
    commands.RegisterHostCommand(
        "volume", {"vol"}, "Set or adjust volume", "volume <0-100|+N|-N>",
        [&player](const ICommandRegistry::Invocation& invocation)
        {
            if (invocation.argc != 2)
            {
                CommandUsage(invocation, "volume <0-100|+N|-N>");
                return;
            }
            int value = 0;
            if (!ParseInt(invocation.argv[1], value))
            {
                CommandUsage(invocation, "volume <0-100|+N|-N>");
                return;
            }
            const std::string arg = invocation.argv[1];
            if (arg.starts_with("+") || arg.starts_with("-"))
            {
                player.AdjustVolume(value);
                CommandInfo(invocation, "Adjusted volume");
                return;
            }
            if (value < 0 || value > 100)
            {
                CommandUsage(invocation, "volume <0-100|+N|-N>");
                return;
            }
            AudioPreferences prefs = player.GetAudioPreferences();
            prefs.defaultVolume = value;
            player.SetAudioPreferences(prefs);
            CommandInfo(invocation, "Volume set");
        }
    );

    commands.RegisterHostCommand(
        "fullscreen", {"fs"}, "Set fullscreen state", "fullscreen on|off|toggle",
        [&window](const ICommandRegistry::Invocation& invocation)
        {
            if (invocation.argc != 2)
            {
                CommandUsage(invocation, "fullscreen on|off|toggle");
                return;
            }
            const std::string arg = invocation.argv[1];
            if (arg == "on")
            {
                window.SetFullscreen(true);
            }
            else if (arg == "off")
            {
                window.SetFullscreen(false);
            }
            else if (arg == "toggle")
            {
                window.SetFullscreen(!window.IsFullscreen());
            }
            else
            {
                CommandUsage(invocation, "fullscreen on|off|toggle");
                return;
            }
            CommandInfo(invocation, "Fullscreen updated");
        }
    );

    commands.RegisterHostCommand(
        "plugins", {}, "List plugins", "plugins",
        [&context](const ICommandRegistry::Invocation& invocation)
        {
            if (invocation.argc != 1)
            {
                CommandUsage(invocation, "plugins");
                return;
            }
            context.Catalog().EnumeratePlugins(
                [](const char* pluginId, const char* displayName, const int*, const char*, const char*, bool enabled,
                   bool loaded, bool loadFailed, void* ud)
                {
                    auto* invocation = static_cast<const ICommandRegistry::Invocation*>(ud);
                    std::ostringstream line;
                    line << pluginId << " [" << (enabled ? "enabled" : "disabled") << ", "
                         << (loaded ? "loaded" : (loadFailed ? "failed" : "not loaded")) << "]";
                    if (displayName && displayName[0] && std::string(displayName) != pluginId)
                    {
                        line << " - " << displayName;
                    }
                    CommandInfo(*invocation, line.str());
                },
                const_cast<ICommandRegistry::Invocation*>(&invocation)
            );
        }
    );
    commands.RegisterHostCommand(
        "plugin", {}, "Enable or disable a plugin", "plugin enable|disable <id>",
        [&context](const ICommandRegistry::Invocation& invocation)
        {
            if (invocation.argc != 3)
            {
                CommandUsage(invocation, "plugin enable|disable <id>");
                return;
            }
            const std::string action = invocation.argv[1];
            if (action != "enable" && action != "disable")
            {
                CommandUsage(invocation, "plugin enable|disable <id>");
                return;
            }

            std::pair<const char*, bool> state{invocation.argv[2], false};
            context.Catalog().EnumeratePlugins(
                [](const char* pluginId, const char*, const int*, const char*, const char*, bool, bool, bool, void* ud)
                {
                    auto* state = static_cast<std::pair<const char*, bool>*>(ud);
                    if (pluginId && std::string(pluginId) == state->first)
                    {
                        state->second = true;
                    }
                },
                &state
            );
            if (!state.second)
            {
                CommandError(invocation, std::string("Unknown plugin: ") + invocation.argv[2]);
                return;
            }
            context.Catalog().SetPluginEnabled(invocation.argv[2], action == "enable");
            CommandInfo(invocation, "Plugin setting saved; restart FrameLift to apply loading changes");
        }
    );

    commands.RegisterHostCommand(
        "settings", {}, "Settings commands", "settings reload",
        [&context](const ICommandRegistry::Invocation& invocation)
        {
            if (invocation.argc != 2 || std::string(invocation.argv[1]) != "reload")
            {
                CommandUsage(invocation, "settings reload");
                return;
            }
            context.Settings().ReloadSettings();
            CommandInfo(invocation, "Settings reloaded");
        }
    );
    commands.RegisterHostCommand(
        "paths", {}, "Show FrameLift paths", "paths",
        [&context, &pluginsPath](const ICommandRegistry::Invocation& invocation)
        {
            if (invocation.argc != 1)
            {
                CommandUsage(invocation, "paths");
                return;
            }
            CommandInfo(
                invocation,
                "prefs: " + BufferString(
                                [&context](char* buf, int cap)
                                {
                                    return context.Paths().GetPrefPath(buf, cap);
                                }
                            )
            );
            CommandInfo(
                invocation,
                "settings: " + BufferString(
                                   [&context](char* buf, int cap)
                                   {
                                       return context.Settings().GetSettingsFilePath(buf, cap);
                                   }
                               )
            );
            CommandInfo(invocation, "plugins: " + pluginsPath);
        }
    );
}
} // namespace host
