// Aegis2D — logical device management.
#pragma once

#include <aegis/export.h>

#include <volk.h>

#include <aegis/core/noncopyable.hpp>
#include <aegis/core/result.hpp>
#include <aegis/core/types.hpp>
#include <aegis/vk/physical_device.hpp>

#include <vector>

namespace aegis::vk {

struct AEGIS_API DeviceConfig {
    /// Optional surface (VK_NULL_HANDLE for headless/offscreen usage).
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    /// Add VK_KHR_swapchain to the device extensions (required for present).
    bool enable_swapchain = true;
    /// When a TRANSFER-only queue family exists, create an additional queue on
    /// it (recommended). Falls back silently to the graphics queue otherwise.
    bool use_dedicated_transfer_queue = true;
};

/// RAII logical device. Holds the graphics queue, the optional present queue
/// and the optional dedicated transfer queue. Move-only.
class AEGIS_API Device : public core::NonCopyable {
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
    /// The dedicated transfer queue when one was created, otherwise the
    /// graphics queue (callers should prefer checking has_dedicated_transfer_queue()
    /// to distinguish the two cases).
    VkQueue transfer_queue() const noexcept {
        return transfer_queue_ != VK_NULL_HANDLE ? transfer_queue_ : graphics_queue_;
    }

    core::u32 graphics_family() const noexcept { return graphics_family_; }
    core::u32 present_family() const noexcept { return present_family_; }
    /// Family of transfer_queue(): the dedicated family when created, else the
    /// graphics family.
    core::u32 transfer_family() const noexcept {
        return transfer_family_ != kInvalidQueueFamily ? transfer_family_ : graphics_family_;
    }
    bool has_present_queue() const noexcept { return present_queue_ != VK_NULL_HANDLE; }
    bool has_dedicated_transfer_queue() const noexcept { return transfer_queue_ != VK_NULL_HANDLE; }

private:
    void reset() noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphics_queue_ = VK_NULL_HANDLE;
    VkQueue present_queue_ = VK_NULL_HANDLE;
    VkQueue transfer_queue_ = VK_NULL_HANDLE;  ///< VK_NULL_HANDLE when not created
    core::u32 graphics_family_ = kInvalidQueueFamily;
    core::u32 present_family_ = kInvalidQueueFamily;
    core::u32 transfer_family_ = kInvalidQueueFamily;
};

} // namespace aegis::vk
