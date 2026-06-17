#pragma once

#include <cstdint>

class IGraphicsBackend;

// Host-internal abstraction over the API-specific video blitter. The FFmpeg player
// uploads decoded RGBA frames + a subtitle overlay through this interface and draws
// them, letterboxed, into the frame the host renders the UI over.
//
// One implementation per graphics backend (GlVideoRenderer, VulkanVideoRenderer);
// each is created by its matching IGraphicsBackend::CreateVideoRenderer(). This is
// host-internal (not part of the plugin ABI), so the signatures can evolve freely.
//
// Every method must run on the thread that owns the graphics context (the host's
// render thread) — never the decode thread.
class IVideoRenderer
{
public:
    virtual ~IVideoRenderer() = default;

    // Build the pipeline / textures against `backend` (the backend that created this
    // renderer — a GL or Vulkan backend). The renderer keeps the pointer to obtain
    // per-frame state (e.g. the current Vulkan command buffer) during Draw(). Returns
    // false if initialisation fails (the caller then just shows black).
    virtual bool Init(IGraphicsBackend* backend) = 0;

    // Upload a tightly packed RGBA8 frame (w*h*4 bytes, top row first).
    virtual void Upload(const uint8_t* rgba, int w, int h) = 0;

    // Zero-copy path (Phase 3, #18): hand off a decoded FFmpeg Vulkan frame whose
    // YCbCr image already lives on the renderer's device. `avFrame` is an AVFrame*
    // (carrying an AVVkFrame) passed as void* to keep libav types out of this header;
    // the Vulkan renderer reads it through the FFmpeg bridge. The frame must stay
    // ref'd by the caller until the next UploadVulkanFrame/Upload. Default no-op so the
    // GL renderer (which never receives Vulkan frames) ignores it.
    virtual void UploadVulkanFrame(void* avFrame, int displayW, int displayH)
    {
        (void)avFrame;
        (void)displayW;
        (void)displayH;
    }

    // Upload the subtitle overlay: a tightly packed RGBA8 image sized to the on-screen
    // video rectangle so it maps 1:1 over the letterboxed video. Straight alpha.
    virtual void UploadOverlay(const uint8_t* rgba, int w, int h) = 0;

    // Clear the framebuffer (fbW x fbH px) to black and, once a frame has been
    // uploaded, draw it centered with aspect-ratio-preserving letterboxing. When
    // drawOverlay is set and an overlay has been uploaded, alpha-composite it over
    // the video within the same letterboxed rectangle.
    virtual void Draw(int fbW, int fbH, bool drawOverlay = false) = 0;
};
