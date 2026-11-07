#include <aegis/vk/physical_device.hpp>

#include <aegis/core/log.hpp>

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace aegis::vk {

using core::u32;
using core::u64;
using core::i64;

namespace {

i64 score_device(const VkPhysicalDeviceProperties& properties, u64 local_bytes, bool unified_queue,
                 bool has_dedicated_transfer) {
    i64 score = 0;
    switch (properties.deviceType) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   score += 1000; break;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: score += 100;  break;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    score += 10;   break;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:            score += 1;    break;
        default:                                     score += 0;    break;
    }
    // +100 points per full GiB of device-local memory, capped at 1 GiB*40 worth.
    constexpr u64 kGiB = 1ull << 30;
    score += static_cast<i64>(std::min<u64>(local_bytes / kGiB, 40)) * 100;
    if (unified_queue) {
        score += 50;
    }
    if (has_dedicated_transfer) {
        score += 100;  // enables async DMA uploads alongside rendering
    }
    return score;
}

} // namespace

core::Result<PhysicalDeviceInfo> select_physical_device(const VkInstance instance,
                                                        const VkSurfaceKHR surface) {
    u32 count = 0;
    if (const VkResult result = vkEnumeratePhysicalDevices(instance, &count, nullptr);
        result != VK_SUCCESS || count == 0) {
        return core::Result<PhysicalDeviceInfo>::failure(
            core::ErrorCode::Unavailable,
            "vkEnumeratePhysicalDevices found no devices (Vulkan 1.3 driver missing?)",
            static_cast<core::i32>(result));
    }

    std::vector<VkPhysicalDevice> devices(count);
    if (const VkResult result = vkEnumeratePhysicalDevices(instance, &count, devices.data());
        result != VK_SUCCESS) {
        return core::Result<PhysicalDeviceInfo>::failure(core::ErrorCode::VulkanFailure,
                                                         "vkEnumeratePhysicalDevices failed",
                                                         static_cast<core::i32>(result));
    }

    const bool needs_present = surface != VK_NULL_HANDLE;

    // Swapchain availability is a device-extension property; checked once.
    auto swapchain_extension_supported = [](VkPhysicalDevice device) {
        u32 ext_count = 0;
        if (vkEnumerateDeviceExtensionProperties(device, nullptr, &ext_count, nullptr) != VK_SUCCESS) {
            return false;
        }
        std::vector<VkExtensionProperties> extensions(ext_count);
        if (vkEnumerateDeviceExtensionProperties(device, nullptr, &ext_count, extensions.data()) !=
            VK_SUCCESS) {
            return false;
        }
        constexpr const char* kSwapchain = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        for (const auto& extension : extensions) {
            if (std::strcmp(extension.extensionName, kSwapchain) == 0) {
                return true;
            }
        }
        return false;
    };

    PhysicalDeviceInfo best{};
    bool found = false;

    for (const VkPhysicalDevice device : devices) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device, &properties);

        // --- Vulkan 1.3 + dynamic rendering + synchronization2 ----------------
        if (VK_API_VERSION_MINOR(properties.apiVersion) < 3) {
            continue;
        }

        VkPhysicalDeviceVulkan13Features vulkan13{};
        vulkan13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.pNext = &vulkan13;
        vkGetPhysicalDeviceFeatures2(device, &features2);

        if (!vulkan13.dynamicRendering || !vulkan13.synchronization2) {
            continue;
        }

        // --- Queue families ----------------------------------------------------
        u32 queue_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_count, nullptr);
        std::vector<VkQueueFamilyProperties> families(queue_count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_count, families.data());

        u32 graphics = kInvalidQueueFamily;
        u32 present = kInvalidQueueFamily;
        u32 dedicated_transfer = kInvalidQueueFamily;

        for (u32 i = 0; i < queue_count; ++i) {
            const VkQueueFlags flags = families[i].queueFlags;
            if (graphics == kInvalidQueueFamily && (flags & VK_QUEUE_GRAPHICS_BIT) != 0) {
                graphics = i;
            }
            // A dedicated transfer family is one that supports TRANSFER but not
            // GRAPHICS or COMPUTE — ideal for DMA uploads overlapping rendering.
            if (dedicated_transfer == kInvalidQueueFamily && (flags & VK_QUEUE_TRANSFER_BIT) != 0 &&
                (flags & VK_QUEUE_GRAPHICS_BIT) == 0 && (flags & VK_QUEUE_COMPUTE_BIT) == 0) {
                dedicated_transfer = i;
            }
            if (needs_present) {
                VkBool32 supported = VK_FALSE;
                if (vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &supported) == VK_SUCCESS &&
                    supported == VK_TRUE) {
                    if (present == kInvalidQueueFamily) {
                        present = i;
                    }
                }
            }
        }

        if (graphics == kInvalidQueueFamily) {
            continue;
        }
        if (needs_present) {
            if (present == kInvalidQueueFamily || !swapchain_extension_supported(device)) {
                continue;
            }
        } else {
            present = graphics;  // headless: present == graphics, never used
        }

        // --- Device-local heap bytes -------------------------------------------
        VkPhysicalDeviceMemoryProperties memory{};
        vkGetPhysicalDeviceMemoryProperties(device, &memory);
        u64 local_bytes = 0;
        for (u32 i = 0; i < memory.memoryHeapCount; ++i) {
            if ((memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0) {
                local_bytes += memory.memoryHeaps[i].size;
            }
        }

        const bool unified = (present == graphics);
        const i64 score = score_device(properties, local_bytes, unified,
                                       dedicated_transfer != kInvalidQueueFamily);

        if (!found || score > best.score) {
            found = true;
            best.handle = device;
            best.properties = properties;
            best.features = features2.features;
            best.vulkan13_features = vulkan13;
            best.memory_properties = memory;
            best.queues.graphics = graphics;
            best.queues.present = present;
            best.queues.dedicated_transfer = dedicated_transfer;
            best.device_local_bytes = local_bytes;
            best.score = score;
        }
    }

    if (!found) {
        return core::Result<PhysicalDeviceInfo>::failure(
            core::ErrorCode::Unavailable,
            "no suitable GPU found (require graphics queue + Vulkan 1.3 with dynamicRendering "
            "+ synchronization2" +
                std::string(needs_present ? " + present support + VK_KHR_swapchain)" : ")"));
    }

    // Vendor/driver versions are the first thing a bug report about surface or
    // swapchain failures needs (e.g. NVIDIA's packed driverVersion on Windows).
    std::ostringstream driver_hex;
    driver_hex << "vendorID=0x" << std::hex << std::setw(4) << std::setfill('0')
               << best.properties.vendorID << " driverVersion=0x" << best.properties.driverVersion
               << " (api " << std::dec << VK_API_VERSION_MAJOR(best.properties.driverVersion) << "."
               << VK_API_VERSION_MINOR(best.properties.driverVersion) << "."
               << VK_API_VERSION_PATCH(best.properties.driverVersion) << ")";
    AEGIS_LOG_INFO("[aegis] selected GPU '" << best.properties.deviceName
                   << "' (" << driver_hex.str()
                   << ", score=" << best.score << ", graphics queue family "
                   << best.queues.graphics << ", present family " << best.queues.present
                   << ", dedicated transfer family "
                   << (best.queues.dedicated_transfer != kInvalidQueueFamily
                           ? std::to_string(best.queues.dedicated_transfer)
                           : std::string("none"))
                   << ")");
    return core::Result<PhysicalDeviceInfo>::ok(std::move(best));
}

} // namespace aegis::vk
