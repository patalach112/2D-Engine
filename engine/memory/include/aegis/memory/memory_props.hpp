// Aegis2D — Vulkan memory-type resolution helpers.
#pragma once

#include <volk.h>

#include <aegis/core/types.hpp>

namespace aegis::memory {

/// Returns VK_MAX_MEMORY_TYPES-in-a-u32 sentinel (~0) when no matching memory
/// type exists, otherwise the index of the first matching type.
[[nodiscard]] inline core::u32 find_memory_type(const VkPhysicalDeviceMemoryProperties& properties,
                                                const core::u32 type_filter,
                                                const VkMemoryPropertyFlags flags) noexcept {
    for (core::u32 i = 0; i < properties.memoryTypeCount; ++i) {
        if ((type_filter & (1u << i)) != 0 &&
            (properties.memoryTypes[i].propertyFlags & flags) == flags) {
            return i;
        }
    }
    return ~core::u32{0};
}

} // namespace aegis::memory
