#include "VulkanGraphicsBackend.h"

#include "VulkanCapabilityPlan.h"
#include "VulkanDeviceSelect.h"
#include "VulkanUtil.h"
#include "VulkanVideoRenderer.h"

#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 1
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#include <vk_mem_alloc.h>

#include <QtCore/QByteArray>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QLibraryInfo>
#include <QtCore/QStandardPaths>
#include <QtCore/QVersionNumber>
#include <QtGui/QGuiApplication>
#include <QtGui/QSurface>
#include <QtGui/QVulkanInstance>
#include <QtGui/QWindow>
#include <QtQuick/QQuickGraphicsConfiguration>
#include <QtQuick/QQuickGraphicsDevice>
#include <QtQuick/QQuickWindow>
#include <QtQuick/QSGRendererInterface>

#include <framelift/Log.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <unordered_set>

namespace
{
bool HasExtension(const std::vector<VkExtensionProperties>& extensions, const char* name)
{
    return std::ranges::any_of(
        extensions,
        [name](const VkExtensionProperties& ext)
        {
            return std::strcmp(ext.extensionName, name) == 0;
        }
    );
}

void AddUniqueExtension(std::vector<std::string>& extensions, const char* name)
{
    if (std::ranges::find(extensions, name) == extensions.end())
    {
        extensions.emplace_back(name);
    }
}

std::string VersionString(uint32_t version)
{
    return std::to_string(VK_API_VERSION_MAJOR(version)) + "." + std::to_string(VK_API_VERSION_MINOR(version)) + "." +
           std::to_string(VK_API_VERSION_PATCH(version));
}

void ThrowVk(const char* message, VkResult result)
{
    throw std::runtime_error(std::string(message) + " (VkResult " + std::to_string(result) + ")");
}

// Validation is on by default in debug builds; FRAMELIFT_VULKAN_VALIDATION=1/0 overrides
// either way (e.g. to debug a release build, or to silence a debug build). Synchronization
// validation is enabled externally through the layer's own settings
// (VK_LAYER_KHRONOS_validation with khronos_validation.validate_sync, or vkconfig).
bool ValidationRequested()
{
    if (const char* env = std::getenv("FRAMELIFT_VULKAN_VALIDATION"); env && *env != '\0')
    {
        return *env == '1';
    }
#ifndef NDEBUG
    return true;
#else
    return false;
#endif
}

VKAPI_ATTR VkBool32 VKAPI_CALL DebugUtilsCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT /*types*/,
    const VkDebugUtilsMessengerCallbackDataEXT* data, void* /*userData*/
)
{
    const char* message = data && data->pMessage ? data->pMessage : "(no message)";
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    {
        Log::Error("Vulkan validation: {}", message);
    }
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    {
        Log::Warn("Vulkan validation: {}", message);
    }
    else
    {
        Log::Debug("Vulkan validation: {}", message);
    }
    return VK_FALSE;
}
} // namespace

VulkanGraphicsBackend::VulkanGraphicsBackend()
{
    CreateInstance();
}

VulkanGraphicsBackend::~VulkanGraphicsBackend()
{
    Shutdown();
}

bool VulkanGraphicsBackend::IsSupported()
{
    try
    {
        VulkanGraphicsBackend probe;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

void VulkanGraphicsBackend::CreateInstance()
{
    uint32_t loaderVersion = VK_API_VERSION_1_0;
    // vkEnumerateInstanceVersion only exists on a 1.1+ loader; resolve it dynamically so a
    // 1.0-only loader leaves loaderVersion at 1.0 (a direct symbol reference is always
    // non-null, so `if (vkEnumerateInstanceVersion)` would never detect the 1.0 case).
    if (auto enumerateVersion =
            reinterpret_cast<PFN_vkEnumerateInstanceVersion>(vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion")))
    {
        enumerateVersion(&loaderVersion);
    }
    if (loaderVersion < VK_API_VERSION_1_3)
    {
        throw std::runtime_error("Vulkan 1.3 loader support is required");
    }
    // Qt's RHI creates its own VMA allocator for the adopted device using this instance
    // API version, and the VMA bundled before Qt 6.10 hard-asserts on anything newer
    // than 1.3 (VMA accepted 1.4 upstream only in 3.2.1). Cap at 1.3 on older Qt — the
    // device negotiation below then routes 1.4-preferred capabilities through their 1.3
    // extension fallbacks (VK_EXT_host_image_copy, VK_KHR_push_descriptor).
    const uint32_t qtApiCap =
        QLibraryInfo::version() >= QVersionNumber(6, 10, 0) ? VK_API_VERSION_1_4 : VK_API_VERSION_1_3;
    instanceApiVersion_ = std::min(loaderVersion, qtApiCap);

    const QByteArrayList preferredExtensions = QQuickGraphicsConfiguration::preferredInstanceExtensions();
    instanceExtNames_.reserve(static_cast<std::size_t>(preferredExtensions.size()) + 2);
    for (const QByteArray& extension : preferredExtensions)
    {
        AddUniqueExtension(instanceExtNames_, extension.constData());
    }
    AddUniqueExtension(instanceExtNames_, VK_KHR_SURFACE_EXTENSION_NAME);

    const QString platform = QGuiApplication::platformName();
    if (platform.contains("wayland", Qt::CaseInsensitive))
    {
        AddUniqueExtension(instanceExtNames_, "VK_KHR_wayland_surface");
    }
    else if (platform.contains("xcb", Qt::CaseInsensitive))
    {
        AddUniqueExtension(instanceExtNames_, "VK_KHR_xcb_surface");
    }
    else if (platform.contains("windows", Qt::CaseInsensitive))
    {
        AddUniqueExtension(instanceExtNames_, "VK_KHR_win32_surface");
    }

    qtInstance_ = std::make_unique<QVulkanInstance>();

    bool debugUtils = false;
    if (ValidationRequested())
    {
        if (qtInstance_->supportedLayers().contains(QByteArrayLiteral("VK_LAYER_KHRONOS_validation")))
        {
            qtInstance_->setLayers({QByteArrayLiteral("VK_LAYER_KHRONOS_validation")});
            validationActive_ = true;
        }
        else
        {
            Log::Warn("Vulkan: validation requested but VK_LAYER_KHRONOS_validation is not installed");
        }
        if (qtInstance_->supportedExtensions().contains(QByteArrayLiteral(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)))
        {
            AddUniqueExtension(instanceExtNames_, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            debugUtils = true;
        }
    }

    QByteArrayList qtExtensions;
    qtExtensions.reserve(static_cast<qsizetype>(instanceExtNames_.size()));
    for (const std::string& extension : instanceExtNames_)
    {
        qtExtensions.push_back(QByteArray::fromStdString(extension));
    }
    qtInstance_->setExtensions(qtExtensions);
    qtInstance_->setApiVersion(
        QVersionNumber(VK_API_VERSION_MAJOR(instanceApiVersion_), VK_API_VERSION_MINOR(instanceApiVersion_))
    );
    if (!qtInstance_->create())
    {
        ThrowVk("Qt Vulkan instance creation failed", qtInstance_->errorCode());
    }
    instance_ = qtInstance_->vkInstance();

    if (debugUtils)
    {
        SetupDebugUtils();
    }
    Log::Debug(
        "Vulkan: loader {}, instance API {}{}", VersionString(loaderVersion), VersionString(instanceApiVersion_),
        validationActive_ ? ", validation on" : ""
    );
}

void VulkanGraphicsBackend::SetupDebugUtils()
{
    VulkanUtil::g_setObjectNameFn =
        reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(vkGetInstanceProcAddr(instance_, "vkSetDebugUtilsObjectNameEXT"));
    destroyDebugMessengerFn_ = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT")
    );
    const auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT")
    );
    if (!createMessenger || !destroyDebugMessengerFn_)
    {
        return;
    }
    VkDebugUtilsMessengerCreateInfoEXT ci{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    ci.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    ci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    ci.pfnUserCallback = DebugUtilsCallback;
    VK_CHECK_LOG(createMessenger(instance_, &ci, nullptr, &debugMessenger_), "Vulkan: debug messenger creation failed");
}

void VulkanGraphicsBackend::ConfigureQtWindow(QQuickWindow* window)
{
    if (!window || configured_)
    {
        return;
    }

    window->setVulkanInstance(qtInstance_.get());

    // CreateDevice() queries QVulkanInstance::supportsPresent(), which requires a live
    // platform window — without one it warns and returns false for every queue family, so
    // no device qualifies and we wrongly fall back to OpenGL. Realizing the *real* window
    // here (an earlier approach) costs it its decorations on some platforms, since it is no
    // longer created lazily at show() time the way the OpenGL path does. So probe present
    // support on a throwaway Vulkan-surface window and leave the real window untouched; it
    // is created normally at show(). setGraphicsDevice() below targets the QQuickWindow
    // object, not a platform window, so it stays valid.
    QWindow probe;
    probe.setSurfaceType(QSurface::VulkanSurface);
    probe.setVulkanInstance(qtInstance_.get());
    probe.create();

    CreateDevice(&probe);
    window->setGraphicsDevice(
        QQuickGraphicsDevice::fromDeviceObjects(
            physicalDevice_, device_, static_cast<int>(graphicsQueueFamily_), static_cast<int>(qtGraphicsQueueIndex_)
        )
    );
    // Frame-ops must be submitted after all recording (uploads in prepare, zero-copy
    // transitions in render) but before Qt submits its scene-graph command buffer;
    // afterRenderPassRecording is exactly that point. No-op on frames without ops.
    QObject::connect(
        window, &QQuickWindow::afterRenderPassRecording, window,
        [this]
        {
            FlushFrameOps();
        },
        Qt::DirectConnection
    );
    // afterFrameEnd fires exactly once per rendered frame, making it the only safe
    // place to advance the retire counter (PrepareQtFrame runs twice per frame — node
    // prepare and render — and double ticks would halve the retire safety margin).
    QObject::connect(
        window, &QQuickWindow::afterFrameEnd, window,
        [this, window]
        {
            FlushFrameSignals();
            const int reported = window->graphicsStateInfo().framesInFlight;
            const uint32_t framesInFlight = reported >= 1 && reported <= static_cast<int>(kMaxFramesInFlight)
                                                ? static_cast<uint32_t>(reported)
                                                : kMaxFramesInFlight;
            retireQueue_.BeginFrame(framesInFlight);
        },
        Qt::DirectConnection
    );
    configured_ = true;
}

void VulkanGraphicsBackend::CreateDevice(QWindow* presentProbe)
{
    uint32_t physicalCount = 0;
    VK_CHECK_THROW(vkEnumeratePhysicalDevices(instance_, &physicalCount, nullptr), "Vulkan device enumeration failed");
    if (physicalCount == 0)
    {
        throw std::runtime_error("No Vulkan physical devices were found");
    }
    std::vector<VkPhysicalDevice> devices(physicalCount);
    VK_CHECK_THROW(vkEnumeratePhysicalDevices(instance_, &physicalCount, devices.data()), "Vulkan device enumeration failed");

    // Gather the Vulkan-side facts per device, then let the pure selection policy
    // (VulkanDeviceSelect, unit-tested) pick the winner.
    static_assert(VulkanDeviceSelect::kMinApiVersion == VK_API_VERSION_1_3);
    struct CandidateData
    {
        uint32_t graphicsFamily = 0;
        std::vector<VkQueueFamilyProperties> queues;
        std::vector<VkExtensionProperties> extensions;
    };
    std::vector<VulkanDeviceSelect::Candidate> candidates(devices.size());
    std::vector<CandidateData> candidateData(devices.size());

    for (std::size_t d = 0; d < devices.size(); ++d)
    {
        VkPhysicalDevice candidate = devices[d];
        VulkanDeviceSelect::Candidate& facts = candidates[d];
        CandidateData& data = candidateData[d];

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(candidate, &properties);
        facts.apiVersion = properties.apiVersion;
        facts.discrete = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
        if (properties.apiVersion < VK_API_VERSION_1_3)
        {
            continue; // remaining queries can't make it eligible
        }

        uint32_t queueCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueCount, nullptr);
        data.queues.resize(queueCount);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueCount, data.queues.data());

        int graphicsFamily = -1;
        for (uint32_t i = 0; i < queueCount; ++i)
        {
            if ((data.queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
                qtInstance_->supportsPresent(candidate, i, presentProbe))
            {
                graphicsFamily = static_cast<int>(i);
                break;
            }
        }
        if (graphicsFamily < 0)
        {
            continue;
        }
        facts.hasGraphicsPresentQueue = true;
        data.graphicsFamily = static_cast<uint32_t>(graphicsFamily);

        uint32_t extensionCount = 0;
        vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, nullptr);
        data.extensions.resize(extensionCount);
        vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, data.extensions.data());
        facts.hasSwapchainExtension = HasExtension(data.extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        if (!facts.hasSwapchainExtension)
        {
            continue;
        }

        VkPhysicalDeviceVulkan13Features candidateF13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceVulkan12Features candidateF12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        VkPhysicalDeviceVulkan11Features candidateF11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
        VkPhysicalDeviceFeatures2 candidateF2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        candidateF2.pNext = &candidateF11;
        candidateF11.pNext = &candidateF12;
        candidateF12.pNext = &candidateF13;
        vkGetPhysicalDeviceFeatures2(candidate, &candidateF2);

        const bool hasDecodeQueue = std::ranges::any_of(
            data.queues,
            [](const VkQueueFamilyProperties& queue)
            {
                return (queue.queueFlags & VK_QUEUE_VIDEO_DECODE_BIT_KHR) != 0;
            }
        );
        facts.videoCapable = HasExtension(data.extensions, VK_KHR_VIDEO_QUEUE_EXTENSION_NAME) &&
                             HasExtension(data.extensions, VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME) &&
                             hasDecodeQueue && candidateF11.samplerYcbcrConversion && candidateF12.timelineSemaphore &&
                             candidateF13.synchronization2 && data.queues[data.graphicsFamily].queueCount >= 2;
    }

    const int chosenIdx = VulkanDeviceSelect::SelectDevice(candidates);
    if (chosenIdx < 0)
    {
        throw std::runtime_error("No Vulkan 1.3 device with a graphics/present queue is available");
    }
    const bool chosenDiscrete = candidates[static_cast<std::size_t>(chosenIdx)].discrete;
    std::vector<VkQueueFamilyProperties> chosenQueues = std::move(candidateData[static_cast<std::size_t>(chosenIdx)].queues);
    std::vector<VkExtensionProperties> chosenExtensions =
        std::move(candidateData[static_cast<std::size_t>(chosenIdx)].extensions);

    physicalDevice_ = devices[static_cast<std::size_t>(chosenIdx)];
    graphicsQueueFamily_ = candidateData[static_cast<std::size_t>(chosenIdx)].graphicsFamily;
    graphicsQueueFlags_ = chosenQueues[graphicsQueueFamily_].queueFlags;

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice_, &properties);
    deviceApiVersion_ = std::min(instanceApiVersion_, properties.apiVersion);
    nvidiaAdapter_ = properties.vendorID == 0x10DE;

    const bool hasHostCopyExt = HasExtension(chosenExtensions, VK_EXT_HOST_IMAGE_COPY_EXTENSION_NAME);

    VkPhysicalDeviceVulkan13Features supportedF13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceVulkan12Features supportedF12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan11Features supportedF11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    VkPhysicalDeviceFeatures2 supportedF2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    VkPhysicalDeviceHostImageCopyFeaturesEXT supportedHostCopy{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_FEATURES_EXT
    };
    supportedF2.pNext = &supportedF11;
    supportedF11.pNext = &supportedF12;
    supportedF12.pNext = &supportedF13;
#ifdef VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES
    VkPhysicalDeviceVulkan14Features supportedF14{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES};
    if (deviceApiVersion_ >= VK_API_VERSION_1_4)
    {
        supportedF13.pNext = &supportedF14;
        supportedF14.pNext = hasHostCopyExt ? &supportedHostCopy : nullptr;
    }
    else if (hasHostCopyExt)
    {
        supportedF13.pNext = &supportedHostCopy;
    }
#else
    if (hasHostCopyExt)
    {
        supportedF13.pNext = &supportedHostCopy;
    }
#endif
    vkGetPhysicalDeviceFeatures2(physicalDevice_, &supportedF2);

    enabledF11_.samplerYcbcrConversion = supportedF11.samplerYcbcrConversion;
    enabledF12_.timelineSemaphore = supportedF12.timelineSemaphore;
    enabledF13_.synchronization2 = supportedF13.synchronization2;
    enabledFeatures2_.features.shaderImageGatherExtended = supportedF2.features.shaderImageGatherExtended;
    enabledFeatures2_.features.fragmentStoresAndAtomics = supportedF2.features.fragmentStoresAndAtomics;
    enabledFeatures2_.features.shaderInt64 = supportedF2.features.shaderInt64;
    enabledFeatures2_.features.vertexPipelineStoresAndAtomics = supportedF2.features.vertexPipelineStoresAndAtomics;
    enabledFeatures2_.pNext = &enabledF11_;
    enabledF11_.pNext = &enabledF12_;
    enabledF12_.pNext = &enabledF13_;
#ifdef VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES
    if (deviceApiVersion_ >= VK_API_VERSION_1_4)
    {
        enabledF13_.pNext = &enabledF14_;
    }
#endif

    // Negotiate the optional 1.4-preferred capabilities (host image copy, push
    // descriptors) with the pure, unit-tested decision table in VulkanCapabilityPlan.
    VulkanCapabilityPlan::Inputs capIn;
    capIn.deviceIs14 = deviceApiVersion_ >= VK_API_VERSION_1_4;
#ifdef VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES
    capIn.coreHostImageCopy = supportedF14.hostImageCopy == VK_TRUE;
    capIn.corePushDescriptor = supportedF14.pushDescriptor == VK_TRUE;
#endif
    capIn.extHostImageCopy = hasHostCopyExt && supportedHostCopy.hostImageCopy == VK_TRUE;
    capIn.extPushDescriptor = HasExtension(chosenExtensions, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
    capIn.hostTransferFormatOk = HostTransferFormatSupported();
    capIn.discreteAdapter = chosenDiscrete;
    if (const char* env = std::getenv("FRAMELIFT_VK_HOST_COPY"); env && *env != '\0')
    {
        capIn.hostCopyEnv = *env == '1' ? 1 : 0;
    }
    if (const char* env = std::getenv("FRAMELIFT_VK_NO_PUSH_DESC"); env && *env == '1')
    {
        capIn.noPushDescEnv = true;
    }
    const VulkanCapabilityPlan::Plan capPlan = VulkanCapabilityPlan::Negotiate(capIn);

    if (capPlan.enableHostCopyFeature)
    {
#ifdef VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES
        if (capIn.deviceIs14)
        {
            enabledF14_.hostImageCopy = VK_TRUE;
        }
        else
#endif
        {
            enabledHostCopy_.hostImageCopy = VK_TRUE;
            enabledF13_.pNext = &enabledHostCopy_;
        }
    }
#ifdef VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES
    if (capPlan.enablePushDescFeature)
    {
        enabledF14_.pushDescriptor = VK_TRUE;
    }
#endif

    enabledDeviceExtNames_.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    auto enableOptional = [&](const char* name)
    {
        if (HasExtension(chosenExtensions, name))
        {
            enabledDeviceExtNames_.push_back(name);
            return true;
        }
        return false;
    };

    const bool hasVideoQueue = enableOptional(VK_KHR_VIDEO_QUEUE_EXTENSION_NAME);
    const bool hasVideoDecodeQueue = enableOptional(VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME);
    const bool videoBase = hasVideoQueue && hasVideoDecodeQueue;
    if (videoBase)
    {
        enableOptional(VK_KHR_VIDEO_DECODE_H264_EXTENSION_NAME);
        enableOptional(VK_KHR_VIDEO_DECODE_H265_EXTENSION_NAME);
#ifdef VK_KHR_VIDEO_DECODE_AV1_EXTENSION_NAME
        enableOptional(VK_KHR_VIDEO_DECODE_AV1_EXTENSION_NAME);
#endif
#ifdef VK_KHR_VIDEO_MAINTENANCE_1_EXTENSION_NAME
        enableOptional(VK_KHR_VIDEO_MAINTENANCE_1_EXTENSION_NAME);
#endif
    }

    for (const char* optional : {
             VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
             VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
         })
    {
        enableOptional(optional);
    }
    if (capPlan.enableHostCopyExt)
    {
        enableOptional(VK_EXT_HOST_IMAGE_COPY_EXTENSION_NAME);
    }
    if (capPlan.enablePushDescExt)
    {
        enableOptional(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
    }

    static constexpr float priorities[] = {1.0f, 1.0f};
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    queueInfos.reserve(chosenQueues.size());
    for (uint32_t i = 0; i < chosenQueues.size(); ++i)
    {
        if (chosenQueues[i].queueCount == 0)
        {
            continue;
        }
        VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueInfo.queueFamilyIndex = i;
        queueInfo.queueCount = i == graphicsQueueFamily_ && chosenQueues[i].queueCount >= 2 ? 2u : 1u;
        queueInfo.pQueuePriorities = priorities;
        queueInfos.push_back(queueInfo);
    }

    enabledDeviceExtPtrs_.reserve(enabledDeviceExtNames_.size());
    for (const std::string& extension : enabledDeviceExtNames_)
    {
        enabledDeviceExtPtrs_.push_back(extension.c_str());
    }

    VkDeviceCreateInfo createInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    createInfo.pNext = &enabledFeatures2_;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
    createInfo.pQueueCreateInfos = queueInfos.data();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(enabledDeviceExtPtrs_.size());
    createInfo.ppEnabledExtensionNames = enabledDeviceExtPtrs_.data();
    const VkResult result = vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_);
    if (result != VK_SUCCESS)
    {
        ThrowVk("Vulkan device creation failed", result);
    }
    qtGraphicsQueueIndex_ = chosenQueues[graphicsQueueFamily_].queueCount >= 2 ? 1u : 0u;
    vkGetDeviceQueue(device_, graphicsQueueFamily_, qtGraphicsQueueIndex_, &graphicsQueue_);
    VulkanUtil::SetObjectName(device_, VK_OBJECT_TYPE_QUEUE, graphicsQueue_, "FrameLift graphics queue (Qt)");

    SetupHostImageCopy(capPlan.useHostCopy);

    pushDescriptors_ = false;
    if (capPlan.usePushDesc)
    {
        pushDescriptorSetFn_ = reinterpret_cast<PFN_vkCmdPushDescriptorSetKHR>(vkGetDeviceProcAddr(
            device_, deviceApiVersion_ >= VK_API_VERSION_1_4 ? "vkCmdPushDescriptorSet" : "vkCmdPushDescriptorSetKHR"
        ));
        pushDescriptors_ = pushDescriptorSetFn_ != nullptr;
    }
    else if (capIn.noPushDescEnv)
    {
        Log::Debug("Vulkan: push descriptors disabled by FRAMELIFT_VK_NO_PUSH_DESC");
    }

    DetectVideoDecodeQueue(chosenQueues);
    if (qtGraphicsQueueIndex_ == 0)
    {
        supportsVulkanVideo_ = false;
        Log::Warn(
            "Vulkan: graphics family exposes one queue; zero-copy disabled because Qt and FFmpeg cannot safely share it"
        );
    }

    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.instance = instance_;
    allocatorInfo.physicalDevice = physicalDevice_;
    allocatorInfo.device = device_;
    allocatorInfo.vulkanApiVersion = deviceApiVersion_;
    if (vmaCreateAllocator(&allocatorInfo, &allocator_) != VK_SUCCESS)
    {
        throw std::runtime_error("Vulkan memory allocator creation failed");
    }

    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = graphicsQueueFamily_;
    if (vkCreateCommandPool(device_, &poolInfo, nullptr, &frameOpsPool_) != VK_SUCCESS)
    {
        throw std::runtime_error("Vulkan frame-ops command pool creation failed");
    }
    VkCommandBufferAllocateInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commandInfo.commandPool = frameOpsPool_;
    commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandInfo.commandBufferCount = static_cast<uint32_t>(frameOpsCmds_.size());
    if (vkAllocateCommandBuffers(device_, &commandInfo, frameOpsCmds_.data()) != VK_SUCCESS)
    {
        throw std::runtime_error("Vulkan frame-ops command buffer allocation failed");
    }
    VulkanUtil::SetObjectName(device_, VK_OBJECT_TYPE_COMMAND_POOL, frameOpsPool_, "FrameLift frame-ops pool");
    for (VkCommandBuffer cmd : frameOpsCmds_)
    {
        VulkanUtil::SetObjectName(device_, VK_OBJECT_TYPE_COMMAND_BUFFER, cmd, "FrameLift frame-ops cmd");
    }

    LoadPipelineCache();

    Log::Debug(
        "Vulkan: {} (device API {}, negotiated {}; zero-copy prerequisites {}, video decode queue {})",
        properties.deviceName, VersionString(properties.apiVersion), VersionString(deviceApiVersion_),
        enabledF11_.samplerYcbcrConversion && enabledF12_.timelineSemaphore && enabledF13_.synchronization2 ? "enabled"
                                                                                                            : "partial",
        supportsVulkanVideo_ ? "available" : "unavailable"
    );
    Log::Debug(
        "Vulkan: Qt graphics queue family {}, index {}; FFmpeg graphics queue index 0", graphicsQueueFamily_,
        qtGraphicsQueueIndex_
    );
}

// The upload format must support host transfer with optimal tiling; folded into the
// capability plan's inputs.
bool VulkanGraphicsBackend::HostTransferFormatSupported() const
{
    VkFormatProperties3 fp3{VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3};
    VkFormatProperties2 fp2{VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2};
    fp2.pNext = &fp3;
    vkGetPhysicalDeviceFormatProperties2(physicalDevice_, VK_FORMAT_R8G8B8A8_UNORM, &fp2);
    return (fp3.optimalTilingFeatures & VK_FORMAT_FEATURE_2_HOST_IMAGE_TRANSFER_BIT_EXT) != 0;
}

// `selected` comes from VulkanCapabilityPlan: support, adapter-type default (see the
// measured discrete-GPU detiling cost documented there) and env override already folded.
void VulkanGraphicsBackend::SetupHostImageCopy(bool selected)
{
    hostImageCopy_ = false;
    if (!selected)
    {
        Log::Debug("Vulkan: host image copy not selected (unsupported, discrete-adapter default, or env override)");
        return;
    }

    const bool coreNames = deviceApiVersion_ >= VK_API_VERSION_1_4;
    transitionImageLayoutFn_ = reinterpret_cast<PFN_vkTransitionImageLayoutEXT>(
        vkGetDeviceProcAddr(device_, coreNames ? "vkTransitionImageLayout" : "vkTransitionImageLayoutEXT")
    );
    copyMemoryToImageFn_ = reinterpret_cast<PFN_vkCopyMemoryToImageEXT>(
        vkGetDeviceProcAddr(device_, coreNames ? "vkCopyMemoryToImage" : "vkCopyMemoryToImageEXT")
    );
    if (!transitionImageLayoutFn_ || !copyMemoryToImageFn_)
    {
        Log::Warn("Vulkan: host image copy entry points missing; falling back to staging uploads");
        transitionImageLayoutFn_ = nullptr;
        copyMemoryToImageFn_ = nullptr;
        return;
    }

    // Sample host-copied images in SHADER_READ_ONLY_OPTIMAL when the implementation
    // accepts it as a copy-dst layout; GENERAL is the spec-guaranteed fallback.
    VkPhysicalDeviceHostImageCopyPropertiesEXT hicProps{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_PROPERTIES_EXT};
    VkPhysicalDeviceProperties2 props2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    props2.pNext = &hicProps;
    vkGetPhysicalDeviceProperties2(physicalDevice_, &props2);
    std::vector<VkImageLayout> dstLayouts(hicProps.copyDstLayoutCount);
    hicProps.pCopyDstLayouts = dstLayouts.data();
    vkGetPhysicalDeviceProperties2(physicalDevice_, &props2);
    hostCopyDstLayout_ = std::ranges::find(dstLayouts, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) != dstLayouts.end()
                             ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                             : VK_IMAGE_LAYOUT_GENERAL;
    hostImageCopy_ = true;
    Log::Debug(
        "Vulkan: host image copy active ({}; dst layout {})", deviceApiVersion_ >= VK_API_VERSION_1_4 ? "1.4 core" : "EXT",
        hostCopyDstLayout_ == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ? "shader-read-only" : "general"
    );
}

bool VulkanGraphicsBackend::HostTransitionImage(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout)
{
    if (!transitionImageLayoutFn_ || image == VK_NULL_HANDLE)
    {
        return false;
    }
    VkHostImageLayoutTransitionInfoEXT info{VK_STRUCTURE_TYPE_HOST_IMAGE_LAYOUT_TRANSITION_INFO_EXT};
    info.image = image;
    info.oldLayout = oldLayout;
    info.newLayout = newLayout;
    info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VK_CHECK_LOG_RETURN(transitionImageLayoutFn_(device_, 1, &info), "Vulkan: host image layout transition failed", false);
    return true;
}

void VulkanGraphicsBackend::CmdPushDescriptorSet(VkCommandBuffer cmd, VkPipelineLayout layout, const VkWriteDescriptorSet& write)
{
    if (pushDescriptorSetFn_ && cmd != VK_NULL_HANDLE)
    {
        pushDescriptorSetFn_(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &write);
    }
}

bool VulkanGraphicsBackend::HostCopyToImage(VkImage image, VkImageLayout layout, const void* pixels, uint32_t w, uint32_t h)
{
    if (!copyMemoryToImageFn_ || image == VK_NULL_HANDLE || !pixels)
    {
        return false;
    }
    VkMemoryToImageCopyEXT region{VK_STRUCTURE_TYPE_MEMORY_TO_IMAGE_COPY_EXT};
    region.pHostPointer = pixels;
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {w, h, 1};
    VkCopyMemoryToImageInfoEXT info{VK_STRUCTURE_TYPE_COPY_MEMORY_TO_IMAGE_INFO_EXT};
    info.dstImage = image;
    info.dstImageLayout = layout;
    info.regionCount = 1;
    info.pRegions = &region;
    VK_CHECK_LOG_RETURN(copyMemoryToImageFn_(device_, &info), "Vulkan: host memory-to-image copy failed", false);
    return true;
}

QString VulkanGraphicsBackend::PipelineCachePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/vulkan_pipeline_cache.bin");
}

void VulkanGraphicsBackend::LoadPipelineCache()
{
    QByteArray blob;
    if (QFile file(PipelineCachePath()); file.open(QIODevice::ReadOnly))
    {
        blob = file.readAll();
    }

    // Only feed data back that this exact device/driver produced; anything else is
    // discarded (the spec requires the application to validate the header).
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physicalDevice_, &props);
    bool blobValid = static_cast<size_t>(blob.size()) >= sizeof(VkPipelineCacheHeaderVersionOne);
    if (blobValid)
    {
        VkPipelineCacheHeaderVersionOne header{};
        std::memcpy(&header, blob.constData(), sizeof(header));
        blobValid = header.headerVersion == VK_PIPELINE_CACHE_HEADER_VERSION_ONE &&
                    header.vendorID == props.vendorID && header.deviceID == props.deviceID &&
                    std::memcmp(header.pipelineCacheUUID, props.pipelineCacheUUID, VK_UUID_SIZE) == 0;
    }

    VkPipelineCacheCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    ci.initialDataSize = blobValid ? static_cast<size_t>(blob.size()) : 0;
    ci.pInitialData = blobValid ? blob.constData() : nullptr;
    if (vkCreatePipelineCache(device_, &ci, nullptr, &pipelineCache_) != VK_SUCCESS && blobValid)
    {
        // Stale/corrupt blob: retry empty rather than running uncached.
        ci.initialDataSize = 0;
        ci.pInitialData = nullptr;
        VK_CHECK_LOG(vkCreatePipelineCache(device_, &ci, nullptr, &pipelineCache_), "Vulkan: pipeline cache creation failed");
    }
}

void VulkanGraphicsBackend::SavePipelineCache()
{
    if (pipelineCache_ == VK_NULL_HANDLE)
    {
        return;
    }
    size_t size = 0;
    if (vkGetPipelineCacheData(device_, pipelineCache_, &size, nullptr) != VK_SUCCESS || size == 0)
    {
        return;
    }
    QByteArray blob(static_cast<qsizetype>(size), Qt::Uninitialized);
    if (vkGetPipelineCacheData(device_, pipelineCache_, &size, blob.data()) != VK_SUCCESS)
    {
        return;
    }
    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::CacheLocation));
    if (QFile file(PipelineCachePath()); file.open(QIODevice::WriteOnly))
    {
        file.write(blob.constData(), static_cast<qsizetype>(size));
    }
}

void VulkanGraphicsBackend::DetectVideoDecodeQueue(const std::vector<VkQueueFamilyProperties>& queueProperties)
{
    supportsVulkanVideo_ = false;
    if (std::ranges::find(enabledDeviceExtNames_, VK_KHR_VIDEO_QUEUE_EXTENSION_NAME) == enabledDeviceExtNames_.end() ||
        std::ranges::find(enabledDeviceExtNames_, VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME) ==
            enabledDeviceExtNames_.end())
    {
        return;
    }

    uint32_t propertyCount = static_cast<uint32_t>(queueProperties.size());
    std::vector<VkQueueFamilyVideoPropertiesKHR> videoProperties(
        propertyCount, {VK_STRUCTURE_TYPE_QUEUE_FAMILY_VIDEO_PROPERTIES_KHR}
    );
    std::vector<VkQueueFamilyProperties2> properties2(propertyCount, {VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2});
    for (uint32_t i = 0; i < propertyCount; ++i)
    {
        properties2[i].pNext = &videoProperties[i];
    }
    vkGetPhysicalDeviceQueueFamilyProperties2(physicalDevice_, &propertyCount, properties2.data());

    for (uint32_t i = 0; i < queueProperties.size(); ++i)
    {
        if (!(queueProperties[i].queueFlags & VK_QUEUE_VIDEO_DECODE_BIT_KHR))
        {
            continue;
        }
        videoDecodeQueueFamily_ = static_cast<int>(i);
        videoDecodeQueueFlags_ = queueProperties[i].queueFlags;
        videoDecodeCaps_ = videoProperties[i].videoCodecOperations;
        vkGetDeviceQueue(device_, i, 0, &videoDecodeQueue_);
        VulkanUtil::SetObjectName(device_, VK_OBJECT_TYPE_QUEUE, videoDecodeQueue_, "FrameLift video decode queue");
        supportsVulkanVideo_ = videoDecodeQueue_ != VK_NULL_HANDLE && videoDecodeCaps_ != 0 &&
                               enabledF11_.samplerYcbcrConversion && enabledF12_.timelineSemaphore &&
                               enabledF13_.synchronization2;
        break;
    }
}

void VulkanGraphicsBackend::OnQtWindowCreated(QQuickWindow* window)
{
    RefreshQtResources(window);
}

void VulkanGraphicsBackend::PrepareQtFrame(QQuickWindow* window)
{
    RefreshQtResources(window);
}

void VulkanGraphicsBackend::RefreshQtResources(QQuickWindow* window)
{
    if (!window || !window->rendererInterface())
    {
        return;
    }
    QSGRendererInterface* renderer = window->rendererInterface();
    if (void* command = renderer->getResource(window, QSGRendererInterface::CommandListResource))
    {
        currentCmd_ = *static_cast<VkCommandBuffer*>(command);
    }
    if (void* pass = renderer->getResource(window, QSGRendererInterface::RenderPassResource))
    {
        renderPass_ = *static_cast<VkRenderPass*>(pass);
    }
    const QSize pixelSize(
        static_cast<int>(window->width() * window->effectiveDevicePixelRatio()),
        static_cast<int>(window->height() * window->effectiveDevicePixelRatio())
    );
    frameExtent_ = {
        static_cast<uint32_t>(std::max(0, pixelSize.width())),
        static_cast<uint32_t>(std::max(0, pixelSize.height())),
    };
    currentFrameSlot_ = static_cast<uint32_t>(std::max(0, window->graphicsStateInfo().currentFrameSlot));
}

std::unique_ptr<IVideoRenderer> VulkanGraphicsBackend::CreateVideoRenderer()
{
    return std::make_unique<VulkanVideoRenderer>(this);
}

void* VulkanGraphicsBackend::GetProcAddr(const char* name) const
{
    return reinterpret_cast<void*>(vkGetInstanceProcAddr(instance_, name));
}

bool VulkanGraphicsBackend::GetVulkanDeviceInfo(VulkanDeviceInfo& out) const noexcept
{
    out.instance = reinterpret_cast<void*>(instance_);
    out.physicalDevice = reinterpret_cast<void*>(physicalDevice_);
    out.device = reinterpret_cast<void*>(device_);
    out.getInstanceProcAddr = reinterpret_cast<void*>(vkGetInstanceProcAddr);
    out.featuresChain = &enabledFeatures2_;
    out.deviceExtensions = enabledDeviceExtPtrs_.empty() ? nullptr : enabledDeviceExtPtrs_.data();
    out.deviceExtensionCount = static_cast<int>(enabledDeviceExtPtrs_.size());
    out.graphicsQueueFamily = static_cast<int>(graphicsQueueFamily_);
    out.graphicsQueueFlags = graphicsQueueFlags_;
    out.videoDecodeQueueFamily = videoDecodeQueueFamily_;
    out.videoDecodeQueueFlags = videoDecodeQueueFlags_;
    out.videoDecodeCaps = videoDecodeCaps_;
    out.supportsVideoDecode = supportsVulkanVideo_;
    out.queueLock = const_cast<VulkanQueueLock*>(&queueLock_);
    out.internalQueueSync = false;
    return device_ != VK_NULL_HANDLE;
}

VkCommandBuffer VulkanGraphicsBackend::FrameOpsCmd()
{
    if (device_ == VK_NULL_HANDLE)
    {
        return VK_NULL_HANDLE;
    }
    if (frameOpsActiveCmd_ == VK_NULL_HANDLE)
    {
        VkCommandBuffer cmd = frameOpsCmds_[currentFrameSlot_ % frameOpsCmds_.size()];
        if (cmd == VK_NULL_HANDLE)
        {
            return VK_NULL_HANDLE;
        }
        // Safe to reset: Qt waited this slot's fence before beginning the frame, which
        // covers our earlier same-queue submit that used this buffer.
        VK_CHECK_LOG(vkResetCommandBuffer(cmd, 0), "Vulkan: frame-ops reset failed");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK_LOG_RETURN(vkBeginCommandBuffer(cmd, &begin), "Vulkan: frame-ops begin failed", VK_NULL_HANDLE);
        frameOpsActiveCmd_ = cmd;
    }
    return frameOpsActiveCmd_;
}

void VulkanGraphicsBackend::AddFrameOpsWait(VkSemaphore semaphore, uint64_t value, VkPipelineStageFlags2 stageMask)
{
    if (semaphore == VK_NULL_HANDLE)
    {
        return;
    }
    VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    wait.semaphore = semaphore;
    wait.value = value;
    wait.stageMask = stageMask;
    frameOpsWaits_.push_back(wait);
}

bool VulkanGraphicsBackend::FlushFrameOps()
{
    if (frameOpsActiveCmd_ == VK_NULL_HANDLE)
    {
        return true; // nothing recorded this frame
    }
    VkCommandBuffer cmd = frameOpsActiveCmd_;
    frameOpsActiveCmd_ = VK_NULL_HANDLE;
    VK_CHECK_LOG(vkEndCommandBuffer(cmd), "Vulkan: frame-ops end failed");

    VkCommandBufferSubmitInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cmdInfo.commandBuffer = cmd;

    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.waitSemaphoreInfoCount = static_cast<uint32_t>(frameOpsWaits_.size());
    submit.pWaitSemaphoreInfos = frameOpsWaits_.empty() ? nullptr : frameOpsWaits_.data();
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmdInfo;

    VkResult result = VK_SUCCESS;
    {
        VulkanQueueGuard guard(queueLock_, graphicsQueueFamily_, qtGraphicsQueueIndex_);
        result = vkQueueSubmit2(graphicsQueue_, 1, &submit, VK_NULL_HANDLE);
    }
    frameOpsWaits_.clear();
    if (result != VK_SUCCESS)
    {
        Log::Error("Vulkan: frame-ops submit failed ({})", static_cast<int>(result));
        return false;
    }
    return true;
}

void VulkanGraphicsBackend::QueueFrameSignal(VkSemaphore semaphore, uint64_t value)
{
    if (semaphore != VK_NULL_HANDLE)
    {
        pendingFrameSignals_.push_back({semaphore, value});
    }
}

void VulkanGraphicsBackend::HostSignalPendingFrameSignals()
{
    if (pendingFrameSignals_.empty() || device_ == VK_NULL_HANDLE)
    {
        return;
    }
    vkDeviceWaitIdle(device_);
    // Queue order is chronological, so per-semaphore values are signalled in
    // increasing order as vkSignalSemaphore requires.
    for (const TimelineSignal& signal : pendingFrameSignals_)
    {
        VkSemaphoreSignalInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO};
        si.semaphore = signal.semaphore;
        si.value = signal.value;
        VK_CHECK_LOG(vkSignalSemaphore(device_, &si), "Vulkan: host signal of pending frame semaphore failed");
    }
    pendingFrameSignals_.clear();
}

void VulkanGraphicsBackend::FlushFrameSignals()
{
    if (pendingFrameSignals_.empty() || device_ == VK_NULL_HANDLE)
    {
        return;
    }

    frameSignalScratch_.clear();
    frameSignalScratch_.reserve(pendingFrameSignals_.size());
    for (const TimelineSignal& signal : pendingFrameSignals_)
    {
        VkSemaphoreSubmitInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        si.semaphore = signal.semaphore;
        si.value = signal.value;
        si.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        frameSignalScratch_.push_back(si);
    }

    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.signalSemaphoreInfoCount = static_cast<uint32_t>(frameSignalScratch_.size());
    submit.pSignalSemaphoreInfos = frameSignalScratch_.data();

    VulkanQueueGuard guard(queueLock_, graphicsQueueFamily_, qtGraphicsQueueIndex_);
    const VkResult result = vkQueueSubmit2(graphicsQueue_, 1, &submit, VK_NULL_HANDLE);
    if (result != VK_SUCCESS)
    {
        Log::Error("Vulkan: zero-copy completion signal submit failed ({})", static_cast<int>(result));
    }
    pendingFrameSignals_.clear();
}

void VulkanGraphicsBackend::DestroyDevice()
{
    if (device_ != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(device_);
    }
    retireQueue_.Drain(); // device idle; free retired objects before their pools/allocator go
    SavePipelineCache();
    if (pipelineCache_ != VK_NULL_HANDLE)
    {
        vkDestroyPipelineCache(device_, pipelineCache_, nullptr);
        pipelineCache_ = VK_NULL_HANDLE;
    }
    if (frameOpsPool_ != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(device_, frameOpsPool_, nullptr);
        frameOpsPool_ = VK_NULL_HANDLE;
        frameOpsCmds_.fill(VK_NULL_HANDLE);
        frameOpsActiveCmd_ = VK_NULL_HANDLE;
    }
    frameOpsWaits_.clear();
    if (allocator_)
    {
        vmaDestroyAllocator(allocator_);
        allocator_ = nullptr;
    }
    if (device_ != VK_NULL_HANDLE)
    {
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }
    hostImageCopy_ = false;
    transitionImageLayoutFn_ = nullptr;
    copyMemoryToImageFn_ = nullptr;
    pushDescriptors_ = false;
    pushDescriptorSetFn_ = nullptr;
}

void VulkanGraphicsBackend::Shutdown()
{
    if (shutdown_)
    {
        return;
    }
    shutdown_ = true;
    currentCmd_ = VK_NULL_HANDLE;
    renderPass_ = VK_NULL_HANDLE;
    HostSignalPendingFrameSignals();
    DestroyDevice();
    if (debugMessenger_ != VK_NULL_HANDLE && destroyDebugMessengerFn_)
    {
        destroyDebugMessengerFn_(instance_, debugMessenger_, nullptr);
        debugMessenger_ = VK_NULL_HANDLE;
    }
    VulkanUtil::g_setObjectNameFn = nullptr;
    qtInstance_.reset();
    instance_ = VK_NULL_HANDLE;
}
