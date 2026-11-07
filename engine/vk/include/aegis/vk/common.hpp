// Aegis2D — small volk-based helpers shared by the vk module.
#pragma once

#include <volk.h>

#include <aegis/core/result.hpp>

namespace aegis::vk {

/// Human-readable name for well-known VkResult values; "VK_RESULT_UNKNOWN" otherwise.
/// Never returns nullptr.
[[nodiscard]] const char* vk_result_to_string(VkResult result) noexcept;

/// VK_SUCCESS -> Result<void>::ok(); anything else -> Result<void>::failure(
/// ErrorCode::VulkanFailure, "what failed: VK_NAME", native=result).
[[nodiscard]] core::Result<void> vk_check(VkResult result, const char* what) noexcept;

} // namespace aegis::vk
