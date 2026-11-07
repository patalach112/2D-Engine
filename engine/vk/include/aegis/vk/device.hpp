// Aegis2D — logical device management.
#pragma once

#include <volk.h>

#include <aegis/core/noncopyable.hpp>
#include <aegis/core/result.hpp>
#include <aegis/core/types.hpp>
#include <aegis/vk/physical_device.hpp>

namespace aegis::vk {

struct DeviceConfig {
    /// Optional surface (VK_NULL_HANDLE for headless/offscreen usage).
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    /// Add VK_KHR_swapchain to the device extensions (required for present).
    bool enable_swapchain = true;
};

/// RAII logical device. Holds the graphics queue and, when the surface lives
/// on a different queue family, the present queue. Move-only.
class Device : public core::NonCopyable {
public:
    Device() = default;
    ~Device();
    Device(Device&& other) noexcept;
    Device& operator=(Device&& other) noexcept;

    /// Enables synchronization2 + dynamicRendering (Vulkan 1.3) always, and
    /// VK_KHR_swapchain when `config.enable_swapchain` is set. On success the
    /// device-level function pointers are loaded via volkLoadDevice().
    [[nodiscard]] static core::Result<Device> create(const PhysicalDeviceInfo& gpu,
                                                     const DeviceConfig& config);

    VkDevice handle() const noexcept { return device_; }
    void wait_idle() const noexcept;

    VkQueue graphics_queue() const noexcept { return graphics_queue_; }
    VkQueue present_queue() const noexcept { return present_queue_ != VK_NULL_HANDLE ? present_queue_ : graphics_queue_; }

    core::u32 graphics_family() const noexcept { return graphics_family_; }
    core::u32 present_family() const noexcept { return present_family_; }
    bool has_present_queue() const noexcept { return present_queue_ != VK_NULL_HANDLE; }

private:
    void reset() noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphics_queue_ = VK_NULL_HANDLE;
    VkQueue present_queue_ = VK_NULL_HANDLE;
    core::u32 graphics_family_ = kInvalidQueueFamily;
    core::u32 present_family_ = kInvalidQueueFamily;
};

} // namespace aegis::vk
