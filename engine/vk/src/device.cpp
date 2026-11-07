#include <aegis/vk/device.hpp>

#include <aegis/core/log.hpp>

#include <set>
#include <string>
#include <vector>

namespace aegis::vk {

namespace {

constexpr float kQueuePriority = 1.0f;

} // namespace

Device::~Device() { reset(); }

Device::Device(Device&& other) noexcept
    : device_(other.device_),
      graphics_queue_(other.graphics_queue_),
      present_queue_(other.present_queue_),
      transfer_queue_(other.transfer_queue_),
      graphics_family_(other.graphics_family_),
      present_family_(other.present_family_),
      transfer_family_(other.transfer_family_) {
    other.device_ = VK_NULL_HANDLE;
    other.graphics_queue_ = VK_NULL_HANDLE;
    other.present_queue_ = VK_NULL_HANDLE;
    other.transfer_queue_ = VK_NULL_HANDLE;
    other.graphics_family_ = kInvalidQueueFamily;
    other.present_family_ = kInvalidQueueFamily;
    other.transfer_family_ = kInvalidQueueFamily;
}

Device& Device::operator=(Device&& other) noexcept {
    if (this != &other) {
        reset();
        device_ = other.device_;
        graphics_queue_ = other.graphics_queue_;
        present_queue_ = other.present_queue_;
        transfer_queue_ = other.transfer_queue_;
        graphics_family_ = other.graphics_family_;
        present_family_ = other.present_family_;
        transfer_family_ = other.transfer_family_;
        other.device_ = VK_NULL_HANDLE;
        other.graphics_queue_ = VK_NULL_HANDLE;
        other.present_queue_ = VK_NULL_HANDLE;
        other.transfer_queue_ = VK_NULL_HANDLE;
        other.graphics_family_ = kInvalidQueueFamily;
        other.present_family_ = kInvalidQueueFamily;
        other.transfer_family_ = kInvalidQueueFamily;
    }
    return *this;
}

core::Result<Device> Device::create(const PhysicalDeviceInfo& gpu, const DeviceConfig& config) {
    if (gpu.handle == VK_NULL_HANDLE) {
        return core::Result<Device>::failure(core::ErrorCode::InvalidArgument,
                                             "Device::create requires a valid PhysicalDeviceInfo");
    }
    if (gpu.queues.graphics == kInvalidQueueFamily) {
        return core::Result<Device>::failure(core::ErrorCode::InvalidArgument,
                                             "PhysicalDeviceInfo has no graphics queue family");
    }

    const core::u32 graphics = gpu.queues.graphics;
    const core::u32 present = gpu.queues.present != kInvalidQueueFamily ? gpu.queues.present : graphics;

    const bool create_transfer_queue =
        config.use_dedicated_transfer_queue && gpu.queues.dedicated_transfer != kInvalidQueueFamily;

    std::set<core::u32> family_set{graphics, present};
    if (create_transfer_queue) {
        family_set.insert(gpu.queues.dedicated_transfer);
    }

    const float priority = kQueuePriority;
    std::vector<VkDeviceQueueCreateInfo> queue_infos;
    queue_infos.reserve(family_set.size());
    for (const core::u32 family : family_set) {
        const VkDeviceQueueCreateInfo info{
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .queueFamilyIndex = family,
            .queueCount = 1,
            .pQueuePriorities = &priority,
        };
        queue_infos.push_back(info);
    }

    VkPhysicalDeviceVulkan13Features vulkan13{};
    vulkan13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    vulkan13.dynamicRendering = VK_TRUE;
    vulkan13.synchronization2 = VK_TRUE;

    std::vector<const char*> extensions;
    if (config.enable_swapchain) {
        extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }

    const VkDeviceCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &vulkan13,
        .flags = 0,
        .queueCreateInfoCount = static_cast<core::u32>(queue_infos.size()),
        .pQueueCreateInfos = queue_infos.data(),
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = static_cast<core::u32>(extensions.size()),
        .ppEnabledExtensionNames = extensions.empty() ? nullptr : extensions.data(),
        .pEnabledFeatures = nullptr,  // feature set lives in vulkan13 (pNext chain)
    };

    Device device;
    if (const VkResult result = vkCreateDevice(gpu.handle, &create_info, nullptr, &device.device_);
        result != VK_SUCCESS) {
        return core::Result<Device>::failure(core::ErrorCode::VulkanFailure,
                                             "vkCreateDevice failed",
                                             static_cast<core::i32>(result));
    }
    volkLoadDevice(device.device_);

    device.graphics_family_ = graphics;
    device.present_family_ = present;
    vkGetDeviceQueue(device.device_, graphics, 0, &device.graphics_queue_);
    if (present != graphics) {
        vkGetDeviceQueue(device.device_, present, 0, &device.present_queue_);
    }
    if (create_transfer_queue) {
        vkGetDeviceQueue(device.device_, gpu.queues.dedicated_transfer, 0, &device.transfer_queue_);
        device.transfer_family_ = gpu.queues.dedicated_transfer;
    } else if (config.use_dedicated_transfer_queue) {
        AEGIS_LOG_INFO("[aegis] no dedicated transfer-only queue family on this GPU; "
                       "uploads will use the graphics queue");
    }

    AEGIS_LOG_INFO("[aegis] logical device created ("
                   << extensions.size() << " device extension(s), " << queue_infos.size()
                   << " queue(s)" << (device.transfer_queue_ != VK_NULL_HANDLE
                                          ? ", dedicated transfer queue"
                                          : "")
                   << ")");
    return core::Result<Device>::ok(std::move(device));
}

void Device::wait_idle() const noexcept {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
    }
}

void Device::reset() noexcept {
    if (device_ != VK_NULL_HANDLE) {
        vkDestroyDevice(device_, nullptr);
    }
    device_ = VK_NULL_HANDLE;
    graphics_queue_ = VK_NULL_HANDLE;
    present_queue_ = VK_NULL_HANDLE;
    transfer_queue_ = VK_NULL_HANDLE;
    graphics_family_ = kInvalidQueueFamily;
    present_family_ = kInvalidQueueFamily;
    transfer_family_ = kInvalidQueueFamily;
}

} // namespace aegis::vk
