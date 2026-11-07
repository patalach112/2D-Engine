// Aegis2D — physical device selection.
#pragma once

#include <volk.h>

#include <aegis/core/result.hpp>
#include <aegis/core/types.hpp>

#include <vector>

namespace aegis::vk {

inline constexpr core::u32 kInvalidQueueFamily = ~core::u32{0};

struct QueueFamilySelection {
    core::u32 graphics = kInvalidQueueFamily;
    core::u32 present = kInvalidQueueFamily;            ///< == graphics for headless usage or unified families
    core::u32 dedicated_transfer = kInvalidQueueFamily; ///< TRANSFER-only family, if the GPU exposes one
};

struct PhysicalDeviceInfo {
    VkPhysicalDevice handle = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties properties{};
    VkPhysicalDeviceFeatures features{};
    VkPhysicalDeviceVulkan13Features vulkan13_features{};
    VkPhysicalDeviceMemoryProperties memory_properties{};

    QueueFamilySelection queues{};
    core::u64 device_local_bytes = 0;  ///< sum of DEVICE_LOCAL heap sizes
    core::i64 score = 0;
};

/// Enumerate physical devices and pick the best one for Aegis2D.
///
/// Eligibility requirements:
///   - a graphics queue family,
///   - Vulkan 1.3 with dynamicRendering + synchronization2,
///   - when `surface` is not VK_NULL_HANDLE: a family with present support
///     (VK_KHR_surface + VK_KHR_swapchain device extension available).
///
/// Scoring: discrete > integrated > virtual > CPU, plus a bonus for larger
/// device-local heaps and unified graphics/present families.
///
/// Pass VK_NULL_HANDLE as `surface` for headless (offscreen) usage; present
/// support is then not required.
[[nodiscard]] core::Result<PhysicalDeviceInfo> select_physical_device(
    VkInstance instance, VkSurfaceKHR surface);

} // namespace aegis::vk
