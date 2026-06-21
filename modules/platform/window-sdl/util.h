#pragma once

#include <algorithm>
#include <cstdio>
#include <cstdlib>

// Prints msg to stderr and exits.
[[noreturn]] inline void Fatal(const char* msg)
{
    fprintf(stderr, "%s\n", msg);
    exit(EXIT_FAILURE);
}

struct WindowSize
{
    int w;
    int h;
};

// Scale (w,h) down to fit within (maxW,maxH) preserving aspect ratio. Never scales
// up (returns the input when it already fits); returns the input unchanged for any
// non-positive dimension. Pure + SDL-free so it can be unit-tested directly.
inline WindowSize FitWithinAspect(int w, int h, int maxW, int maxH)
{
    if (w <= 0 || h <= 0 || maxW <= 0 || maxH <= 0 || (w <= maxW && h <= maxH))
    {
        return {w, h};
    }
    const float scale =
        std::min(static_cast<float>(maxW) / static_cast<float>(w), static_cast<float>(maxH) / static_cast<float>(h));
    return {static_cast<int>(static_cast<float>(w) * scale), static_cast<int>(static_cast<float>(h) * scale)};
}