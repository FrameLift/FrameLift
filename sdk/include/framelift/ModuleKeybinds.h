#pragma once

#include <functional>
#include <string>

namespace framelift
{

struct Keybind
{
    const char* label;
    const char* action;
    std::string* storage;
    const char* def;
    std::function<void()> onPress;
};

} // namespace framelift
