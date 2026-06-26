// Service interfaces — the capability surface plugins discover with
// ctx.GetService<T>(). Cross-plugin communication is events-first (see
// <framelift/Events.h>); a service exists where a synchronous query is genuinely
// needed (IHistory) or where the host exposes a capability (settings, the package
// catalogue, fonts, paths). Adding a service never bumps FRAMELIFT_ABI_VERSION.
#pragma once
#include <framelift/services/IAppPaths.h>
#include <framelift/services/IHistory.h>
#include <framelift/services/IJson.h>
#include <framelift/services/ILogBuffer.h>
#include <framelift/services/IPackageCatalog.h>
#include <framelift/services/ISettingsRegistry.h>
#include <framelift/services/ISettingsStore.h>
