#pragma once

// Diagnostic info about the active graphics presentation backend. A capability
// service — discover it with ctx.GetService<IGraphicsInfo>() and null-check.
// The backend is selected once at startup (FL_BACKEND env var) and never switches
// at runtime, so the reported values are session-constant once the window exists.
class IGraphicsInfo
{
public:
    static constexpr const char* InterfaceId = "framelift.IGraphicsInfo";
    virtual ~IGraphicsInfo() = default;

    // Human-readable name of the active API ("OpenGL" / "Vulkan"), NUL-terminated.
    // Writes up to cap bytes into buf and returns the full length excl. NUL; pass
    // buf=nullptr to query the required size. Reports "unknown" before the backend
    // is created (the window has not been shown yet).
    virtual int GetBackendName(char* buf, int cap) const noexcept = 0;

    // True when the active graphics adapter is NVIDIA.
    [[nodiscard]] virtual bool HasNvidiaAdapter() const noexcept = 0;
};
