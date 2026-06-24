#pragma once

// Application filesystem locations. A capability service — discover it with
// ctx.GetService<IAppPaths>().
class IAppPaths
{
public:
    static constexpr const char* InterfaceId = "framelift.IAppPaths";
    virtual ~IAppPaths() = default;

    // Trailing-separator user config dir (e.g. "C:/Users/foo/AppData/Roaming/FrameLift/").
    // Returns full length excl. NUL; pass buf=nullptr to query required size.
    virtual int GetPrefPath(char* buf, int cap) const noexcept = 0;
};
