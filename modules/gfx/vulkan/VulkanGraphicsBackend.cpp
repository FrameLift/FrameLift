#include "VulkanGraphicsBackend.h"

#include "VulkanVideoRenderer.h"

#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 1
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#include <vk_mem_alloc.h>

#include <QtCore/QByteArray>
#include <QtCore/QVersionNumber>
#include <QtGui/QGuiApplication>
#include <QtGui/QVulkanInstance>
#include <QtQuick/QQuickGraphicsConfiguration>
#include <QtQuick/QQuickGraphicsDevice>
#include <QtQuick/QQuickWindow>
#include <QtQuick/QSGRendererInterface>

#include <framelift/Log.h>

#include <algorithm>
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
    if (vkEnumerateInstanceVersion)
    {
        vkEnumerateInstanceVersion(&loaderVersion);
    }
    if (loaderVersion < VK_API_VERSION_1_3)
    {
        throw std::runtime_error("Vulkan 1.3 loader support is required");
    }
    instanceApiVersion_ = std::min(loaderVersion, VK_API_VERSION_1_4);

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

    uint32_t availableCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &availableCount, nullptr);
    std::vector<VkExtensionProperties> available(availableCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &availableCount, available.data());

    std::vector<const char*> extensions;
    extensions.reserve(instanceExtNames_.size());
    for (const std::string& extension : instanceExtNames_)
    {
        if (!HasExtension(available, extension.c_str()))
        {
            throw std::runtime_error("Required Qt Vulkan instance extension is unavailable: " + extension);
        }
        extensions.push_back(extension.c_str());
    }

    qtInstance_ = std::make_unique<QVulkanInstance>();
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
    if (platform.contains("wayland", Qt::CaseInsensitive))
    {
        const bool rawWaylandSurface =
            vkGetInstanceProcAddr(instance_, "vkCreateWaylandSurfaceKHR") != nullptr;
        const bool qtWaylandSurface = qtInstance_->getInstanceProcAddr("vkCreateWaylandSurfaceKHR") != nullptr;
        Log::Debug(
            "Vulkan: Wayland surface proc lookup raw={}, qt={}", rawWaylandSurface ? "available" : "missing",
            qtWaylandSurface ? "available" : "missing"
        );
    }
    Log::Debug("Vulkan: loader {}, instance API {}", VersionString(loaderVersion), VersionString(instanceApiVersion_));
}

void VulkanGraphicsBackend::ConfigureQtWindow(QQuickWindow* window)
{
    if (!window || configured_)
    {
        return;
    }

    window->setVulkanInstance(qtInstance_.get());

    CreateDevice(window);
    window->setGraphicsDevice(
        QQuickGraphicsDevice::fromDeviceObjects(
            physicalDevice_, device_, static_cast<int>(graphicsQueueFamily_), static_cast<int>(qtGraphicsQueueIndex_)
        )
    );
    QObject::connect(
        window, &QQuickWindow::afterFrameEnd, window,
        [this]
        {
            FlushFrameSignals();
        },
        Qt::DirectConnection
    );
    configured_ = true;
}

void VulkanGraphicsBackend::CreateDevice(QQuickWindow* window)
{
    uint32_t physicalCount = 0;
    vkEnumeratePhysicalDevices(instance_, &physicalCount, nullptr);
    if (physicalCount == 0)
    {
        throw std::runtime_error("No Vulkan physical devices were found");
    }
    std::vector<VkPhysicalDevice> devices(physicalCount);
    vkEnumeratePhysicalDevices(instance_, &physicalCount, devices.data());

    VkPhysicalDevice chosen = VK_NULL_HANDLE;
    uint32_t chosenGraphics = 0;
    bool chosenDiscrete = false;
    bool chosenVideoCapable = false;
    std::vector<VkQueueFamilyProperties> chosenQueues;
    std::vector<VkExtensionProperties> chosenExtensions;

    for (VkPhysicalDevice candidate : devices)
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(candidate, &properties);
        if (properties.apiVersion < VK_API_VERSION_1_3)
        {
            continue;
        }

        uint32_t queueCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueCount, nullptr);
        std::vector<VkQueueFamilyProperties> queues(queueCount);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueCount, queues.data());

        int graphicsFamily = -1;
        for (uint32_t i = 0; i < queueCount; ++i)
        {
            if ((queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && qtInstance_->supportsPresent(candidate, i, window))
            {
                graphicsFamily = static_cast<int>(i);
                break;
            }
        }
        if (graphicsFamily < 0)
        {
            continue;
        }

        uint32_t extensionCount = 0;
        vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> extensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, extensions.data());
        if (!HasExtension(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
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
            queues,
            [](const VkQueueFamilyProperties& queue)
            {
                return (queue.queueFlags & VK_QUEUE_VIDEO_DECODE_BIT_KHR) != 0;
            }
        );
        const bool candidateVideoCapable = HasExtension(extensions, VK_KHR_VIDEO_QUEUE_EXTENSION_NAME) &&
                                           HasExtension(extensions, VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME) &&
                                           hasDecodeQueue && candidateF11.samplerYcbcrConversion &&
                                           candidateF12.timelineSemaphore && candidateF13.synchronization2 &&
                                           queues[static_cast<uint32_t>(graphicsFamily)].queueCount >= 2;
        const bool discrete = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
        if (!chosen || (candidateVideoCapable && !chosenVideoCapable) ||
            (candidateVideoCapable == chosenVideoCapable && discrete && !chosenDiscrete))
        {
            chosen = candidate;
            chosenGraphics = static_cast<uint32_t>(graphicsFamily);
            chosenDiscrete = discrete;
            chosenVideoCapable = candidateVideoCapable;
            chosenQueues = std::move(queues);
            chosenExtensions = std::move(extensions);
        }
    }

    if (!chosen)
    {
        throw std::runtime_error("No Vulkan 1.3 device with a graphics/present queue is available");
    }

    physicalDevice_ = chosen;
    graphicsQueueFamily_ = chosenGraphics;
    graphicsQueueFlags_ = chosenQueues[chosenGraphics].queueFlags;

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice_, &properties);
    deviceApiVersion_ = std::min(instanceApiVersion_, properties.apiVersion);
    nvidiaAdapter_ = properties.vendorID == 0x10DE;

    VkPhysicalDeviceVulkan13Features supportedF13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceVulkan12Features supportedF12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan11Features supportedF11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    VkPhysicalDeviceFeatures2 supportedF2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    supportedF2.pNext = &supportedF11;
    supportedF11.pNext = &supportedF12;
    supportedF12.pNext = &supportedF13;
#ifdef VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES
    VkPhysicalDeviceVulkan14Features supportedF14{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES};
    if (deviceApiVersion_ >= VK_API_VERSION_1_4)
    {
        supportedF13.pNext = &supportedF14;
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
    if (vkCreateCommandPool(device_, &poolInfo, nullptr, &immediatePool_) != VK_SUCCESS)
    {
        throw std::runtime_error("Vulkan upload command pool creation failed");
    }
    VkCommandBufferAllocateInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commandInfo.commandPool = immediatePool_;
    commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandInfo.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(device_, &commandInfo, &immediateCmd_) != VK_SUCCESS)
    {
        throw std::runtime_error("Vulkan upload command buffer allocation failed");
    }
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (vkCreateFence(device_, &fenceInfo, nullptr, &immediateFence_) != VK_SUCCESS)
    {
        throw std::runtime_error("Vulkan upload fence creation failed");
    }

    Log::Info(
        "Vulkan: {} (device API {}, negotiated {}; zero-copy prerequisites {}, video decode queue {})",
        properties.deviceName, VersionString(properties.apiVersion), VersionString(deviceApiVersion_),
        enabledF11_.samplerYcbcrConversion && enabledF12_.timelineSemaphore && enabledF13_.synchronization2 ? "enabled"
                                                                                                            : "partial",
        supportsVulkanVideo_ ? "available" : "unavailable"
    );
    Log::Info(
        "Vulkan: Qt graphics queue family {}, index {}; FFmpeg graphics queue index 0", graphicsQueueFamily_,
        qtGraphicsQueueIndex_
    );
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

bool VulkanGraphicsBackend::SubmitFrameTransition(VkCommandBuffer cmd, VkSemaphore waitSemaphore, uint64_t waitValue)
{
    if (cmd == VK_NULL_HANDLE || waitSemaphore == VK_NULL_HANDLE)
    {
        return false;
    }

    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkTimelineSemaphoreSubmitInfo timeline{VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
    timeline.waitSemaphoreValueCount = 1;
    timeline.pWaitSemaphoreValues = &waitValue;

    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.pNext = &timeline;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &waitSemaphore;
    submit.pWaitDstStageMask = &waitStage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;

    VulkanQueueGuard guard(queueLock_, graphicsQueueFamily_, qtGraphicsQueueIndex_);
    return vkQueueSubmit(graphicsQueue_, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS;
}

void VulkanGraphicsBackend::QueueFrameSignal(VkSemaphore semaphore, uint64_t value)
{
    if (semaphore != VK_NULL_HANDLE)
    {
        pendingFrameSignals_.push_back({semaphore, value});
    }
}

void VulkanGraphicsBackend::FlushFrameSignals()
{
    if (pendingFrameSignals_.empty() || device_ == VK_NULL_HANDLE)
    {
        return;
    }

    std::vector<VkSemaphore> semaphores;
    std::vector<uint64_t> values;
    semaphores.reserve(pendingFrameSignals_.size());
    values.reserve(pendingFrameSignals_.size());
    for (const TimelineSignal& signal : pendingFrameSignals_)
    {
        semaphores.push_back(signal.semaphore);
        values.push_back(signal.value);
    }

    VkTimelineSemaphoreSubmitInfo timeline{VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
    timeline.signalSemaphoreValueCount = static_cast<uint32_t>(values.size());
    timeline.pSignalSemaphoreValues = values.data();

    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.pNext = &timeline;
    submit.signalSemaphoreCount = static_cast<uint32_t>(semaphores.size());
    submit.pSignalSemaphores = semaphores.data();

    VulkanQueueGuard guard(queueLock_, graphicsQueueFamily_, qtGraphicsQueueIndex_);
    const VkResult result = vkQueueSubmit(graphicsQueue_, 1, &submit, VK_NULL_HANDLE);
    if (result != VK_SUCCESS)
    {
        Log::Error("Vulkan: zero-copy completion signal submit failed ({})", static_cast<int>(result));
    }
    pendingFrameSignals_.clear();
}

bool VulkanGraphicsBackend::ImmediateSubmit(void (*record)(VkCommandBuffer, void*), void* ud)
{
    if (!record || immediateCmd_ == VK_NULL_HANDLE)
    {
        return false;
    }
    vkResetFences(device_, 1, &immediateFence_);
    vkResetCommandBuffer(immediateCmd_, 0);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(immediateCmd_, &begin) != VK_SUCCESS)
    {
        return false;
    }
    record(immediateCmd_, ud);
    if (vkEndCommandBuffer(immediateCmd_) != VK_SUCCESS)
    {
        return false;
    }
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &immediateCmd_;
    VulkanQueueGuard guard(queueLock_, graphicsQueueFamily_, qtGraphicsQueueIndex_);
    if (vkQueueSubmit(graphicsQueue_, 1, &submit, immediateFence_) != VK_SUCCESS)
    {
        return false;
    }
    return vkWaitForFences(device_, 1, &immediateFence_, VK_TRUE, UINT64_MAX) == VK_SUCCESS;
}

void VulkanGraphicsBackend::DestroyDevice()
{
    if (device_ != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(device_);
    }
    if (immediateFence_ != VK_NULL_HANDLE)
    {
        vkDestroyFence(device_, immediateFence_, nullptr);
        immediateFence_ = VK_NULL_HANDLE;
    }
    if (immediatePool_ != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(device_, immediatePool_, nullptr);
        immediatePool_ = VK_NULL_HANDLE;
        immediateCmd_ = VK_NULL_HANDLE;
    }
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
    pendingFrameSignals_.clear();
    DestroyDevice();
    qtInstance_.reset();
    instance_ = VK_NULL_HANDLE;
}
