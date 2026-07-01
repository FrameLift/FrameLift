#pragma once

#include <string>

class CommandRegistry;
class FFmpegPlayer;
class IAppWindow;
class ModuleContext;

namespace host
{
void RegisterCoreCommands(
    CommandRegistry& commands, ModuleContext& context, FFmpegPlayer& player, IAppWindow& window,
    const std::string& pluginsPath
);
}
