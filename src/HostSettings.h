#pragma once

#include "Settings.h"

// Application composition of all built-in settings sections. The settings
// module remains generic; only this host layer knows the complete module set.
class HostSettings final : public Settings
{
public:
    HostSettings();
    void ApplyLaunchEnvironmentOverrides();
};
