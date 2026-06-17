#pragma once

#include "../gfx/VulkanDeviceInfo.h"

struct AVBufferRef;

// Bridge between the volk-based Vulkan renderer and FFmpeg's Vulkan hwaccel (Phase 3,
// #18). This is the ONLY place that mixes libav's Vulkan headers with our handles, so
// it lives in its own TU that never includes volk (the two are mutually exclusive in a
// single translation unit). Callers pass neutral PODs (VulkanDeviceInfo / VulkanFrameInfo
// from VulkanDeviceInfo.h); this side reinterprets the raw handles back to Vulkan types.

// Build an AV_HWDEVICE_TYPE_VULKAN device context that WRAPS the renderer's existing
// instance/physical-device/device/queues (never lets FFmpeg create its own — the decoded
// images must be usable by the renderer). Returns an owned AVBufferRef* (av_buffer_unref
// to release), or nullptr on failure.
AVBufferRef* CreateVulkanHwDevice(const VulkanDeviceInfo& info);

// Read the primary image handle + timeline-sync state of a decoded Vulkan frame
// (`avFrame` is an AVFrame* whose data[0] is an AVVkFrame). Takes the frame lock around
// the read. Returns false if `avFrame` is not an AV_PIX_FMT_VULKAN frame. The renderer
// uses the result to build a view, barrier the image, and register the wait with the
// backend's queue submission.
bool GetVulkanFrameInfo(void* avFrame, VulkanFrameInfo& out);

// Write back the image's post-sample state (layout / incremented timeline value / owning
// queue family) so FFmpeg and the next consumer observe correct state. Called by the
// renderer after it has recorded the sample and computed the signal value.
void SetVulkanFrameState(void* avFrame, int newLayout, unsigned long long newSemValue, unsigned int newQueueFamily);
