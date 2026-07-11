#pragma once

#include <cstdint>
#include <memory>

#include "GraphicsApi.h"
#include "IVideoRenderer.h"
#if FRAMELIFT_MODULE_GRAPHICS_VULKAN
#include "VulkanDeviceInfo.h"
#endif

class QQuickWindow;

// Host-internal abstraction over the graphics API. It owns everything API-specific
// behind the Qt scene graph: the adopted GL context or Vulkan device, renderers, and
// UI/video resources. Under Qt the window is a QQuickWindow owned by QtAppWindow; the
// video is drawn as a QSGRenderNode inside the scene-graph render pass.
//
// Not part of the plugin ABI — signatures may evolve as the Vulkan backend lands.
// GL/Qt implementations may #include <QtGui/...> and system GL headers (same allowance
// as QtAppWindow). All methods run on the host's main / scene-graph render thread (the
// GUI thread, with the "basic" QSG render loop forced).
class IGraphicsBackend
{
public:
    virtual ~IGraphicsBackend() = default;

    // Called once Qt's scene-graph OpenGL context exists and is current (from the first
    // VideoRenderNode::render()): adopt that context and resolve the GL entry points the
    // backend needs. The backend does NOT create or own the context — Qt does.
    // Configure a newly-created QQuickWindow before its scene graph initializes.
    // Vulkan uses this to hand FrameLift's instance/device to Qt; OpenGL is a no-op.
    virtual void ConfigureQtWindow(QQuickWindow* window) = 0;

    // Called from QSGRenderNode::prepare()/render() once the scene graph is active.
    // Backends adopt or refresh Qt-owned per-frame resources here.
    virtual void OnQtWindowCreated(QQuickWindow* window) = 0;
    virtual void PrepareQtFrame(QQuickWindow* window) = 0;

    // Release any GL resources the backend created. Called while Qt's GL context is still
    // current (before the QQuickWindow is torn down).
    virtual void Shutdown() = 0;

    // Human-readable name of the active API ("OpenGL" / "Vulkan"), for diagnostics.
    [[nodiscard]] virtual const char* Name() const = 0;

    // True when the active graphics adapter is NVIDIA. Host-internal diagnostic/
    // settings hint used to expose CUDA-only decode modes only when they can work.
    [[nodiscard]] virtual bool HasNvidiaAdapter() const noexcept
    {
        return false;
    }

    // Create the video renderer paired with this backend (GlVideoRenderer for the GL
    // backend, VulkanVideoRenderer for Vulkan). The player owns the returned renderer
    // and calls IVideoRenderer::Init(this) on it.
    [[nodiscard]] virtual std::unique_ptr<IVideoRenderer> CreateVideoRenderer() = 0;

#if FRAMELIFT_MODULE_GRAPHICS_VULKAN
    // Fill `out` with the live Vulkan instance/device/queues so the FFmpeg Vulkan
    // hwaccel can WRAP this device for zero-copy decode (Phase 3, #18). Returns false
    // for non-Vulkan backends (the default) — the caller then uses the CPU-RGBA8 path.
    // Stays Vulkan-type-free (VulkanDeviceInfo is a neutral POD) so graphics-core
    // does not inherit Vulkan or FFmpeg headers.
    [[nodiscard]] virtual bool GetVulkanDeviceInfo(VulkanDeviceInfo& out) const noexcept
    {
        (void)out;
        return false;
    }
#endif

    // ── Backend utilities ─────────────────────────────────────────────────────
    [[nodiscard]] virtual void* GetProcAddr(const char* name) const = 0;
};

// Create the presentation backend for `api`. Phase 1 implements OpenGL only; any other
// value logs a warning and falls back to OpenGL.
std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(GraphicsApi api);
