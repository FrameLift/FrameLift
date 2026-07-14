#pragma once

#include "VulkanDeviceInfo.h"

struct AVBufferRef;

// Bridge between the Vulkan renderer and FFmpeg's Vulkan hwaccel (Phase 3, #18).
// This is the ONLY place that mixes libav's Vulkan headers with our handles. Callers
// pass neutral PODs (VulkanDeviceInfo / VulkanFrameInfo from VulkanDeviceInfo.h);
// this side reinterprets the raw handles back to Vulkan types.

// Build an AV_HWDEVICE_TYPE_VULKAN device context that WRAPS the renderer's existing
// instance/physical-device/device/queues (never lets FFmpeg create its own — the decoded
// images must be usable by the renderer). Returns an owned AVBufferRef* (av_buffer_unref
// to release), or nullptr on failure.
AVBufferRef* CreateVulkanHwDevice(const VulkanDeviceInfo& info);

// Stable identity of the underlying AVVkFrame, shared by cloned AVFrame references.
// Used only to reject duplicate presentation of an already locked in-flight image.
void* GetVulkanFrameIdentity(void* avFrame) noexcept;

// Movable ownership of an AVVkFrame's lock. The renderer keeps this alive from its
// state snapshot through Qt's queue submission, preventing FFmpeg from scheduling the
// same image's next decode/DPB use before our completion signal is queued.
class LockedVulkanFrame
{
public:
    LockedVulkanFrame() = default;
    ~LockedVulkanFrame();

    LockedVulkanFrame(const LockedVulkanFrame&) = delete;
    LockedVulkanFrame& operator=(const LockedVulkanFrame&) = delete;
    LockedVulkanFrame(LockedVulkanFrame&& other) noexcept;
    LockedVulkanFrame& operator=(LockedVulkanFrame&& other) noexcept;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return vulkanFrame_ != nullptr;
    }

    [[nodiscard]] const VulkanFrameInfo& Info() const noexcept
    {
        return info_;
    }

    [[nodiscard]] void* Identity() const noexcept
    {
        return frameIdentity_;
    }

    // Publish a state only after its matching signal has actually been submitted.
    // Both Commit and Unlock release the AVVkFrame lock and empty this object.
    void Commit(int newLayout, unsigned long long newSemValue, unsigned int newQueueFamily) noexcept;
    void Unlock() noexcept;

private:
    friend bool LockVulkanFrame(void* avFrame, LockedVulkanFrame& out);

    void* frameIdentity_ = nullptr;
    void* retainedFrame_ = nullptr;
    void* framesContext_ = nullptr;
    void* vulkanFrame_ = nullptr;
    VulkanFrameInfo info_{};
};

// Lock and snapshot one single-image AV_PIX_FMT_VULKAN frame. On success `out`
// remains locked until it is committed, explicitly unlocked, or destroyed.
bool LockVulkanFrame(void* avFrame, LockedVulkanFrame& out);
