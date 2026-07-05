#pragma once

#include "VideoFrameDesc.h" // layout of the pixels channel (libav/GL/Vulkan-free)

#include <atomic>
#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

// Decode→render video frame handoff: a latest-wins single-slot mailbox.
//
// Two channels share the slot (a loaded file uses exactly one for its lifetime):
//   pixels — CPU frame buffers (RGBA or planar YUV, described by VideoFrameDesc)
//            cycling decode-owned → pending → display-owned by
//            swap, so steady-state playback does no allocation;
//   opaque — an owned foreign frame handle (the Vulkan zero-copy AVFrame),
//            stored as void* with an injected release callback so this header
//            stays libav-free and standalone-testable.
//
// Threading: Publish* is the decode/video-worker side, Acquire/Commit/Display*
// the render-thread side; mutex_ guards only the pending slot. display*
// members are render-thread-owned (ReleaseAll excepted: it must only run once
// the producer and renderer are gone — see ~FFmpegPlayer).
class VideoFrameGate
{
public:
    struct AcquireResult
    {
        bool newPixels = false;
        bool newOpaque = false;
        int w = 0;
        int h = 0;
        VideoFrameDesc desc; // pixels channel only; desc.w/h mirror w/h there
    };

    // Must be set before the first PublishOpaque; not thread-safe to change while
    // frames are in flight.
    void SetOpaqueRelease(void (*release)(void*))
    {
        release_ = release;
    }

    // Hand a finished CPU frame (layout in desc) to the render thread; src is swapped
    // with the pending buffer (latest wins) so the producer gets a reusable buffer back.
    void PublishPixels(std::vector<uint8_t>& src, const VideoFrameDesc& desc)
    {
        {
            std::lock_guard lock(mutex_);
            std::swap(src, pendingPixels_);
            pendingDesc_ = desc;
            pendingW_ = desc.w;
            pendingH_ = desc.h;
            pendingValid_ = true;
            pendingIsOpaque_ = false;
        }
        newFramePending_ = true;
    }

    // Hand an owned opaque frame to the render thread, releasing a still-unconsumed
    // pending one (latest wins). The gate owns `frame` from here on.
    void PublishOpaque(void* frame, int w, int h)
    {
        {
            std::lock_guard lock(mutex_);
            ReleaseLocked(pendingOpaque_);
            pendingOpaque_ = frame;
            pendingW_ = w;
            pendingH_ = h;
            pendingValid_ = true;
            pendingIsOpaque_ = true;
        }
        newFramePending_ = true;
    }

    [[nodiscard]] bool HasNewFrame() const
    {
        return newFramePending_.load();
    }

    // Render thread: adopt the pending frame (if any) into the display slot and
    // clear the new-frame flag. An adopted opaque frame replaces (releases) the
    // previously displayed one; pixels swap buffers.
    [[nodiscard]] AcquireResult Acquire()
    {
        AcquireResult r;
        {
            std::lock_guard lock(mutex_);
            if (pendingValid_)
            {
                if (pendingIsOpaque_)
                {
                    ReleaseLocked(displayOpaque_);
                    displayOpaque_ = pendingOpaque_;
                    pendingOpaque_ = nullptr;
                    r.newOpaque = true;
                }
                else
                {
                    std::swap(displayPixels_, pendingPixels_);
                    displayDesc_ = pendingDesc_;
                    r.newPixels = true;
                    r.desc = pendingDesc_;
                }
                r.w = pendingW_;
                r.h = pendingH_;
                pendingValid_ = false;
            }
        }
        newFramePending_ = false;
        return r;
    }

    // Render thread, once the renderer is ready: latch which channel the display
    // slot is on. Switching opaque → pixels (e.g. a new software-decoded file)
    // drops the held opaque frame so its pool/device can be released.
    void CommitDisplayChannel(const AcquireResult& r)
    {
        if (r.newOpaque)
        {
            displayIsOpaque_ = true;
        }
        else if (r.newPixels)
        {
            displayIsOpaque_ = false;
            ReleaseLocked(displayOpaque_);
        }
    }

    [[nodiscard]] bool DisplayIsOpaque() const
    {
        return displayIsOpaque_;
    }

    [[nodiscard]] void* DisplayOpaque() const
    {
        return displayOpaque_;
    }

    [[nodiscard]] const std::vector<uint8_t>& DisplayPixels() const
    {
        return displayPixels_;
    }

    // Layout of the display-slot pixels (render-thread-owned, valid after the first
    // pixels Acquire) — lets the renderer re-upload without a fresh publish.
    [[nodiscard]] const VideoFrameDesc& DisplayDesc() const
    {
        return displayDesc_;
    }

    // Release every held opaque frame (pending and displayed). Call only when the
    // producer threads are joined and the renderer's views over the displayed
    // frame are gone.
    void ReleaseAll()
    {
        std::lock_guard lock(mutex_);
        ReleaseLocked(pendingOpaque_);
        ReleaseLocked(displayOpaque_);
        pendingValid_ = pendingValid_ && !pendingIsOpaque_;
    }

    ~VideoFrameGate()
    {
        ReleaseAll();
    }

private:
    // Despite the name this needs no lock itself; callers hold mutex_ where the
    // pointer is shared (pendingOpaque_) and the display slot is thread-owned.
    void ReleaseLocked(void*& p)
    {
        if (p && release_)
        {
            release_(p);
        }
        p = nullptr;
    }

    void (*release_)(void*) = nullptr;

    std::mutex mutex_; // guards the pending slot below
    std::vector<uint8_t> pendingPixels_;
    VideoFrameDesc pendingDesc_;
    void* pendingOpaque_ = nullptr;
    int pendingW_ = 0;
    int pendingH_ = 0;
    bool pendingValid_ = false;
    bool pendingIsOpaque_ = false;
    std::atomic<bool> newFramePending_{false};

    // Render-thread-owned display slot.
    std::vector<uint8_t> displayPixels_;
    VideoFrameDesc displayDesc_;
    void* displayOpaque_ = nullptr;
    bool displayIsOpaque_ = false;
};
