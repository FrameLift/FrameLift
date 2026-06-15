#pragma once

#include <string>

// Pure command-line parsing for the host. Extracted so it can be unit-tested
// without pulling in SDL/FFmpeg. The host only needs one thing from argv: the
// file/URL to open on startup. The full argument vector is handed to plugins via
// CliCommandEvent, so anything richer (subcommands, flags) is their concern.

// Return the first positional argument (the open target — a file path or URL),
// or "" when there is none. Arguments starting with '-' are treated as flags and
// skipped, so unknown flags never get mistaken for a file and the app still
// launches normally. Scanning starts at index 1 (argv[0] is the program name).
inline std::string ParseOpenTarget(const int argc, const char* const* argv)
{
    if (!argv)
    {
        return {};
    }
    for (int i = 1; i < argc; ++i)
    {
        const char* arg = argv[i];
        if (arg && arg[0] && arg[0] != '-')
        {
            return arg;
        }
    }
    return {};
}
