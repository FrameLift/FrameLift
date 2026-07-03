#pragma once

#include <cstdint>

// Capability negotiation for the optional Vulkan 1.4-preferred paths (host image
// copy, push descriptors): which feature/extension to enable and whether to actually
// use the path, given device support, adapter type and env overrides. Pure and
// Vulkan-free (the VulkanQueueLock.h precedent) so the decision table is
// unit-testable; VulkanGraphicsBackend fills the inputs from its Vulkan queries.
namespace VulkanCapabilityPlan
{
struct Inputs
{
    bool deviceIs14 = false;          // negotiated device API >= 1.4
    bool coreHostImageCopy = false;   // Vulkan14Features::hostImageCopy
    bool extHostImageCopy = false;    // VK_EXT_host_image_copy present with its feature bit
    bool hostTransferFormatOk = false; // R8G8B8A8 optimal tiling supports host transfer
    bool discreteAdapter = false;
    int hostCopyEnv = -1;             // FRAMELIFT_VK_HOST_COPY: -1 unset, else 0/1
    bool corePushDescriptor = false;  // Vulkan14Features::pushDescriptor
    bool extPushDescriptor = false;   // VK_KHR_push_descriptor present
    bool noPushDescEnv = false;       // FRAMELIFT_VK_NO_PUSH_DESC=1
};

struct Plan
{
    bool enableHostCopyFeature = false; // enable the feature (core bit or EXT struct)
    bool enableHostCopyExt = false;     // additionally enable VK_EXT_host_image_copy (1.3 path)
    bool useHostCopy = false;           // actually route uploads through it
    bool enablePushDescFeature = false; // core 1.4 feature bit
    bool enablePushDescExt = false;     // VK_KHR_push_descriptor (1.3 path)
    bool usePushDesc = false;
};

constexpr Plan Negotiate(const Inputs& in)
{
    Plan plan{};

    const bool hostCopySupported = in.deviceIs14 ? in.coreHostImageCopy : in.extHostImageCopy;
    plan.enableHostCopyFeature = hostCopySupported;
    plan.enableHostCopyExt = hostCopySupported && !in.deviceIs14;
    // Default: integrated/UMA only — on discrete GPUs the driver's CPU-side detiling
    // makes host copies slower than the staging path (measured; see SetupHostImageCopy).
    const bool hostCopyWanted = in.hostCopyEnv < 0 ? !in.discreteAdapter : in.hostCopyEnv == 1;
    plan.useHostCopy = hostCopySupported && in.hostTransferFormatOk && hostCopyWanted;

    const bool pushDescSupported = in.deviceIs14 ? in.corePushDescriptor : in.extPushDescriptor;
    plan.enablePushDescFeature = in.deviceIs14 && in.corePushDescriptor;
    plan.enablePushDescExt = pushDescSupported && !in.deviceIs14;
    plan.usePushDesc = pushDescSupported && !in.noPushDescEnv;

    return plan;
}
} // namespace VulkanCapabilityPlan
