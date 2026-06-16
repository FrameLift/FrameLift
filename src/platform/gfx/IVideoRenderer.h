#pragma once

#include <cstdint>
#include <memory>

// Host-internal abstraction over the API-specific video blitter. The FFmpeg player
// uploads decoded RGBA frames + a subtitle overlay through this interface and draws
// them, letterboxed, into the current framebuffer that the host renders the UI over.
//
// Currently OpenGL only (GlVideoRenderer); a Vulkan implementation follows. This is
// host-internal (not part of the plugin ABI), so the signatures can evolve freely —
// in particular Init() takes a GL-style proc loader today and will take a graphics
// device handle once the Vulkan backend lands.
//
// Every method must run on the thread that owns the graphics context (the host's
// render thread) — never the decode thread.
class IVideoRenderer
{
public:
    virtual ~IVideoRenderer() = default;

    // Resolve graphics functions and build the pipeline / textures. Returns false if
    // initialisation fails (the caller then just clears to black).
    // getProcAddr(name, ud) resolves a GL function (GL backend only).
    virtual bool Init(void* (*getProcAddr)(const char* name, void* ud), void* ud) = 0;

    // Upload a tightly packed RGBA8 frame (w*h*4 bytes, top row first).
    virtual void Upload(const uint8_t* rgba, int w, int h) = 0;

    // Upload the subtitle overlay: a tightly packed RGBA8 image sized to the on-screen
    // video rectangle so it maps 1:1 over the letterboxed video. Straight alpha.
    virtual void UploadOverlay(const uint8_t* rgba, int w, int h) = 0;

    // Clear the framebuffer (fbW x fbH px) to black and, once a frame has been
    // uploaded, draw it centered with aspect-ratio-preserving letterboxing. When
    // drawOverlay is set and an overlay has been uploaded, alpha-composite it over
    // the video within the same letterboxed rectangle.
    virtual void Draw(int fbW, int fbH, bool drawOverlay = false) = 0;
};

// Create the video renderer for the active graphics backend. Phase 1 always returns
// the OpenGL implementation; the Vulkan path is added alongside VulkanGraphicsBackend.
std::unique_ptr<IVideoRenderer> CreateVideoRenderer();
