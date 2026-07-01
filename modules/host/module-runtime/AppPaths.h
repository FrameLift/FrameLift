#pragma once
#include <framelift/services/IAppPaths.h>
#include <string>

// Read-only application path provider. Owned by ModuleContext and registered under
// IAppPaths::InterfaceId; plugins reach it via ctx.GetService<IAppPaths>().
class AppPaths final : public IAppPaths
{
public:
    explicit AppPaths(std::string prefPath) : prefPath_(std::move(prefPath)) {}

    int GetPrefPath(char* buf, int cap) const noexcept override;

private:
    std::string prefPath_;
};
