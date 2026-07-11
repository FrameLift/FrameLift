#pragma once

// Tracks the decoded dimensions most recently announced through VideoReconfig.
// The tracker belongs to the player rather than a video worker because seeking
// restarts that worker without changing the active video's configuration.
class FFmpegVideoConfigTracker
{
public:
    void Reset() noexcept
    {
        width_ = 0;
        height_ = 0;
    }

    [[nodiscard]] bool Update(int width, int height) noexcept
    {
        if (width <= 0 || height <= 0 || (width == width_ && height == height_))
        {
            return false;
        }
        width_ = width;
        height_ = height;
        return true;
    }

private:
    int width_ = 0;
    int height_ = 0;
};
