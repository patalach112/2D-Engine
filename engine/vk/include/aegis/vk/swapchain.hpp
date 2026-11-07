// Aegis2D — swapchain management (+ recreation on resize / suboptimal).
#pragma once

#include <volk.h>

#include <aegis/core/noncopyable.hpp>
#include <aegis/core/result.hpp>
#include <aegis/core/types.hpp>
#include <aegis/vk/device.hpp>
#include <aegis/vk/physical_device.hpp>

#include <vector>

namespace aegis::vk {

struct SwapchainConfig {
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    core::u32 width = 1280;
    core::u32 height = 720;
    /// FIFO is always supported; mailbox is preferred for low-latency rendering
    /// when the driver offers it.
    bool prefer_mailbox = true;
};

class Swapchain : public core::NonCopyable {
public:
    Swapchain() = default;
    ~Swapchain();
    Swapchain(Swapchain&& other) noexcept;
    Swapchain& operator=(Swapchain&& other) noexcept;

    [[nodiscard]] static core::Result<Swapchain> create(const Device& device,
                                                        const PhysicalDeviceInfo& gpu,
                                                        const SwapchainConfig& config);

    /// Destroys the current swapchain and creates a new one for the given size,
    /// chaining oldSwapchain as the compatible predecessor. Returns false (and
    /// leaves the current swapchain intact) if recreation fails.
    bool recreate(core::u32 width, core::u32 height);

    VkSwapchainKHR handle() const noexcept { return swapchain_; }
    VkExtent2D extent() const noexcept { return extent_; }
    VkFormat image_format() const noexcept { return image_format_; }
    const std::vector<VkImage>& images() const noexcept { return images_; }
    const std::vector<VkImageView>& image_views() const noexcept { return image_views_; }

private:
    void destroy_resources() noexcept;
    void reset() noexcept;

    VkDevice device_ = VK_NULL_HANDLE;        ///< non-owning; the Device must outlive this swapchain
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;   ///< non-owning
    PhysicalDeviceInfo gpu_{};
    SwapchainConfig config_{};

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat image_format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_{};
    std::vector<VkImage> images_;
    std::vector<VkImageView> image_views_;
};

} // namespace aegis::vk
