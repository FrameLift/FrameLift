#pragma once

#include <cmath>

// Aspect-ratio-preserving letterbox / pillarbox math for the FFmpeg backend
// (issue #8, Phase 5). Pure integer/double math with no libav / GL include, so it
// builds in the standalone native test suite and is shared by both the video
// renderer (to place the video quad) and the player (to size the libass subtitle
// overlay to the same on-screen rectangle).

struct LetterboxRect
{
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

// Fit a texW×texH image into an fbW×fbH framebuffer preserving aspect ratio,
// centered. Returns the on-screen viewport rect. Degenerate inputs (any <= 0)
// yield the (clamped) full framebuffer so callers never divide by zero.
inline LetterboxRect ComputeLetterbox(int fbW, int fbH, int texW, int texH)
{
    if (fbW <= 0 || fbH <= 0 || texW <= 0 || texH <= 0)
    {
        return {0, 0, fbW > 0 ? fbW : 0, fbH > 0 ? fbH : 0};
    }

    const double videoAR = static_cast<double>(texW) / texH;
    const double fbAR = static_cast<double>(fbW) / fbH;
    int vpW = fbW;
    int vpH = fbH;
    if (fbAR > videoAR)
    {
        // Framebuffer is wider than the video → pillarbox (bars left/right).
        vpH = fbH;
        vpW = static_cast<int>(std::lround(fbH * videoAR));
    }
    else
    {
        // Framebuffer is taller than the video → letterbox (bars top/bottom).
        vpW = fbW;
        vpH = static_cast<int>(std::lround(fbW / videoAR));
    }
    return {(fbW - vpW) / 2, (fbH - vpH) / 2, vpW, vpH};
}
