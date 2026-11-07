#include <aegis/vk/swapchain.hpp>

#include <aegis/core/log.hpp>

#include <algorithm>
#include <cstring>
#include <limits>
#include <vector>

#include <array>
namespace aegis::vk {

using core::u32;

namespace {

struct SwapchainResources {
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    std::vector<VkImage> images;
    std::vector<VkImageView> views;
};

core::Result<SwapchainResources> build_resources(const VkDevice device, const PhysicalDeviceInfo& gpu,
                                                 const SwapchainConfig& config,
                                                 const VkSwapchainKHR old_swapchain) {
    const VkPhysicalDevice physical = gpu.handle;
    const VkSurfaceKHR surface = config.surface;

    VkSurfaceCapabilitiesKHR caps{};
    if (const VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps);
        result != VK_SUCCESS) {
        return core::Result<SwapchainResources>::failure(core::ErrorCode::VulkanFailure,
                                                         "vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed",
                                                         static_cast<core::i32>(result));
    }

    // ---- Format: prefer 8-bit BGRA sRGB, fall back to the first advertised format.
    u32 format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &format_count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &format_count, formats.data());

    VkSurfaceFormatKHR chosen = formats[0];
    for (const auto& candidate : formats) {
        if (candidate.format == VK_FORMAT_B8G8R8A8_SRGB &&
            candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = candidate;
            break;
        }
    }

    // ---- Present mode: mailbox when preferred+supported, else FIFO (guaranteed).
    u32 mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &mode_count, nullptr);
    std::vector<VkPresentModeKHR> modes(mode_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &mode_count, modes.data());

    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
    if (config.prefer_mailbox) {
        for (const auto mode : modes) {
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
                present_mode = mode;
                break;
            }
        }
    }

    // ---- Extent: fixed by the driver when possible.
    VkExtent2D extent{};
    if (caps.currentExtent.width != std::numeric_limits<u32>::max()) {
        extent = caps.currentExtent;
    } else {
        extent.width = std::clamp(config.width, caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = std::clamp(config.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    }

    u32 image_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && image_count > caps.maxImageCount) {
        image_count = caps.maxImageCount;
    }

    // ---- Queue family sharing ------------------------------------------------
    const u32 graphics = gpu.queues.graphics;
    const u32 present = gpu.queues.present;
    const bool shared_queues = graphics != present;
    const std::vector<u32> shared_families{graphics, present};

    // ---- Composite alpha: prefer opaque, else the first advertised mode.
    VkCompositeAlphaFlagBitsKHR composite_alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if ((caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) == 0) {
        constexpr std::array<VkCompositeAlphaFlagBitsKHR, 3> kCandidates{
            VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
        };
        for (const auto candidate : kCandidates) {
            if ((caps.supportedCompositeAlpha & static_cast<VkCompositeAlphaFlagsKHR>(candidate)) != 0) {
                composite_alpha = candidate;
                break;
            }
        }
    }

    const VkSwapchainCreateInfoKHR create_info{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = nullptr,
        .flags = 0,
        .surface = surface,
        .minImageCount = image_count,
        .imageFormat = chosen.format,
        .imageColorSpace = chosen.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .imageSharingMode = shared_queues ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = shared_queues ? static_cast<u32>(shared_families.size()) : 0,
        .pQueueFamilyIndices = shared_queues ? shared_families.data() : nullptr,
        .preTransform = caps.currentTransform,
        .compositeAlpha = composite_alpha,
        .presentMode = present_mode,
        .clipped = VK_TRUE,
        .oldSwapchain = old_swapchain,
    };

    SwapchainResources resources;
    resources.format = chosen.format;
    resources.extent = extent;

    if (const VkResult result = vkCreateSwapchainKHR(device, &create_info, nullptr, &resources.swapchain);
        result != VK_SUCCESS) {
        return core::Result<SwapchainResources>::failure(core::ErrorCode::VulkanFailure,
                                                         "vkCreateSwapchainKHR failed",
                                                         static_cast<core::i32>(result));
    }

    u32 image_count_out = 0;
    vkGetSwapchainImagesKHR(device, resources.swapchain, &image_count_out, nullptr);
    resources.images.resize(image_count_out);
    vkGetSwapchainImagesKHR(device, resources.swapchain, &image_count_out, resources.images.data());

    resources.views.reserve(resources.images.size());
    for (const VkImage image : resources.images) {
        const VkImageViewCreateInfo view_info{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .image = image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = resources.format,
            .components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                           VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY},
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        };
        VkImageView view = VK_NULL_HANDLE;
        if (const VkResult result = vkCreateImageView(device, &view_info, nullptr, &view);
            result != VK_SUCCESS) {
            for (const VkImageView created : resources.views) {
                vkDestroyImageView(device, created, nullptr);
            }
            vkDestroySwapchainKHR(device, resources.swapchain, nullptr);
            return core::Result<SwapchainResources>::failure(core::ErrorCode::VulkanFailure,
                                                             "vkCreateImageView failed",
                                                             static_cast<core::i32>(result));
        }
        resources.views.push_back(view);
    }

    AEGIS_LOG_INFO("[aegis] swapchain created: " << extent.width << "x" << extent.height << " ("
                   << resources.images.size() << " images, "
                   << (present_mode == VK_PRESENT_MODE_MAILBOX_KHR ? "mailbox" : "fifo")
                   << " present)");
    return core::Result<SwapchainResources>::ok(std::move(resources));
}

} // namespace

Swapchain::~Swapchain() { reset(); }

Swapchain::Swapchain(Swapchain&& other) noexcept
    : device_(other.device_),
      surface_(other.surface_),
      gpu_(other.gpu_),
      config_(other.config_),
      swapchain_(other.swapchain_),
      image_format_(other.image_format_),
      extent_(other.extent_),
      images_(std::move(other.images_)),
      image_views_(std::move(other.image_views_)) {
    other.device_ = VK_NULL_HANDLE;
    other.surface_ = VK_NULL_HANDLE;
    other.swapchain_ = VK_NULL_HANDLE;
    other.image_format_ = VK_FORMAT_UNDEFINED;
}

Swapchain& Swapchain::operator=(Swapchain&& other) noexcept {
    if (this != &other) {
        reset();
        device_ = other.device_;
        surface_ = other.surface_;
        gpu_ = other.gpu_;
        config_ = other.config_;
        swapchain_ = other.swapchain_;
        image_format_ = other.image_format_;
        extent_ = other.extent_;
        images_ = std::move(other.images_);
        image_views_ = std::move(other.image_views_);
        other.device_ = VK_NULL_HANDLE;
        other.surface_ = VK_NULL_HANDLE;
        other.swapchain_ = VK_NULL_HANDLE;
        other.image_format_ = VK_FORMAT_UNDEFINED;
    }
    return *this;
}

core::Result<Swapchain> Swapchain::create(const Device& device, const PhysicalDeviceInfo& gpu,
                                          const SwapchainConfig& config) {
    if (config.surface == VK_NULL_HANDLE || config.width == 0 || config.height == 0) {
        return core::Result<Swapchain>::failure(core::ErrorCode::InvalidArgument,
                                                "Swapchain requires a valid surface and non-zero size");
    }

    auto resources = build_resources(device.handle(), gpu, config, VK_NULL_HANDLE);
    if (!resources) {
        return core::Result<Swapchain>::failure(resources.error().code, resources.error().message,
                                                resources.error().native_code);
    }

    Swapchain swapchain;
    swapchain.device_ = device.handle();
    swapchain.surface_ = config.surface;
    swapchain.gpu_ = gpu;
    swapchain.config_ = config;
    SwapchainResources taken = std::move(resources).take();
    swapchain.swapchain_ = taken.swapchain;
    swapchain.image_format_ = taken.format;
    swapchain.extent_ = taken.extent;
    swapchain.images_ = std::move(taken.images);
    swapchain.image_views_ = std::move(taken.views);
    return core::Result<Swapchain>::ok(std::move(swapchain));
}

bool Swapchain::recreate(const u32 width, const u32 height) {
    if (device_ == VK_NULL_HANDLE || surface_ == VK_NULL_HANDLE || width == 0 || height == 0) {
        return false;
    }
    SwapchainConfig config = config_;
    config.width = width;
    config.height = height;

    auto resources = build_resources(device_, gpu_, config, swapchain_);
    if (!resources) {
        AEGIS_LOG_ERROR("[aegis] swapchain recreation failed: " << resources.error().to_string());
        return false;  // old swapchain stays valid
    }

    // Adopt the new resources, then release the old swapchain.
    auto views_to_destroy = std::move(image_views_);
    const VkSwapchainKHR old_swapchain = swapchain_;

    SwapchainResources adopted = std::move(resources).take();
    swapchain_ = adopted.swapchain;
    image_format_ = adopted.format;
    extent_ = adopted.extent;
    images_ = std::move(adopted.images);
    image_views_ = std::move(adopted.views);
    config_ = config;

    for (const VkImageView view : views_to_destroy) {
        vkDestroyImageView(device_, view, nullptr);
    }
    vkDestroySwapchainKHR(device_, old_swapchain, nullptr);
    return true;
}

void Swapchain::destroy_resources() noexcept {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    for (const VkImageView view : image_views_) {
        vkDestroyImageView(device_, view, nullptr);
    }
    image_views_.clear();
    images_.clear();
    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
}

void Swapchain::reset() noexcept {
    destroy_resources();
    device_ = VK_NULL_HANDLE;
    surface_ = VK_NULL_HANDLE;
    image_format_ = VK_FORMAT_UNDEFINED;
}

} // namespace aegis::vk
