#pragma once

// Internal command registry exposed by the host. Commands are FrameLift actions,
// not shell/process execution. Plugins can register commands and invoke the
// registry through this POD callback surface without changing IModuleContext.
class ICommandRegistry
{
public:
    static constexpr const char* InterfaceId = "framelift.ICommandRegistry";
    virtual ~ICommandRegistry() = default;

    enum OutputLevel : int
    {
        OutputInfo = 0,
        OutputWarning = 1,
        OutputError = 2,
    };

    using OutputFn = void (*)(void* ud, int level, const char* text) noexcept;

    struct Invocation
    {
        int argc = 0;
        const char* const* argv = nullptr;
        OutputFn output = nullptr;
        void* outputUd = nullptr;
    };

    using HandlerFn = void (*)(const Invocation* invocation, void* ud) noexcept;

    struct CommandSpec
    {
        const char* name = nullptr;
        const char* aliases = nullptr; // space/comma/pipe separated
        const char* summary = nullptr;
        const char* usage = nullptr;
        HandlerFn handler = nullptr;
        void* ud = nullptr;
        void (*cleanup)(void* ud) noexcept = nullptr;
    };

    using Visitor =
        void (*)(const char* name, const char* aliases, const char* summary, const char* usage, void* ud) noexcept;

    // Returns false when the spec is invalid or any name/alias already exists.
    [[nodiscard]] virtual bool RegisterCommand(const CommandSpec* spec) noexcept = 0;

    virtual void EnumerateCommands(Visitor visit, void* ud) const noexcept = 0;

    // Parses and executes a command line. Diagnostics are emitted to output.
    [[nodiscard]] virtual bool Execute(const char* line, OutputFn output, void* outputUd) noexcept = 0;
};
