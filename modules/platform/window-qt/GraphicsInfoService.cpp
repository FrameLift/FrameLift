#include "GraphicsInfoService.h"

#include <cstring>

#include "IGraphicsBackend.h"
#include "QtAppWindow.h"

namespace
{
IGraphicsBackend* Backend(const QtAppWindow* window) noexcept
{
    return window ? static_cast<IGraphicsBackend*>(window->GetGraphicsBackend()) : nullptr;
}
} // namespace

int GraphicsInfoService::GetBackendName(char* buf, const int cap) const noexcept
{
    const auto* backend = Backend(window_);
    const char* name = backend ? backend->Name() : "unknown";
    const int len = static_cast<int>(std::strlen(name));
    if (buf && cap > 0)
    {
        const int n = len < cap - 1 ? len : cap - 1;
        std::memcpy(buf, name, static_cast<std::size_t>(n));
        buf[n] = '\0';
    }
    return len;
}

bool GraphicsInfoService::HasNvidiaAdapter() const noexcept
{
    const auto* backend = Backend(window_);
    return backend && backend->HasNvidiaAdapter();
}
