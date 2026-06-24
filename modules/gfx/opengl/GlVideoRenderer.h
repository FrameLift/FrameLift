#pragma once

#include <cstdint>
#include <memory>

#include "IVideoRenderer.h"

// GL 3.3-core blitter: uploads software-decoded RGBA frames to a texture and draws
// them, letterboxed, to the current framebuffer (the default FBO the host renders
// the UI over).
//
// It resolves its own GL entry points through the backend's GetProcAddr, so it needs
// no system GL headers and no link dependency beyond the live GL context. Every
// method must run on the thread that owns that context (the host's main / render
// thread) — never the decode thread.
class GlVideoRenderer final : public IVideoRenderer
{
public:
    GlVideoRenderer();
    ~GlVideoRenderer() override;

    GlVideoRenderer(const GlVideoRenderer&) = delete;
    GlVideoRenderer& operator=(const GlVideoRenderer&) = delete;

    bool Init(IGraphicsBackend* backend) override;
    void Upload(const uint8_t* rgba, int w, int h) override;
    void UploadOverlay(const uint8_t* rgba, int w, int h) override;
    void Draw(int fbW, int fbH, bool drawOverlay = false) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
