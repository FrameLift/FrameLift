#pragma once

#include <framelift/platform/IGraphicsInfo.h>

class QtAppWindow;

// Host adapter exposing the active graphics backend to plugins as IGraphicsInfo.
// It forwards to the concrete IGraphicsBackend owned by the window (the backend
// only exists once the window's scene graph is up), reporting "unknown" until then.
class GraphicsInfoService final : public IGraphicsInfo
{
public:
    explicit GraphicsInfoService(const QtAppWindow* window) noexcept : window_(window)
    {
    }

    int GetBackendName(char* buf, int cap) const noexcept override;
    [[nodiscard]] bool HasNvidiaAdapter() const noexcept override;

private:
    const QtAppWindow* window_ = nullptr;
};
