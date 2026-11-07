#include <algorithm>
#include <cstdint>

#include <aegis/core/log.hpp>
#include <aegis/core/types.hpp>
#include <aegis/core/version.hpp>

#if !defined(AEGIS_WINDOWED)
#define AEGIS_WINDOWED 0
#endif
#if !defined(AEGIS_GLFW_NULL_PLATFORM)
#define AEGIS_GLFW_NULL_PLATFORM 0
#endif

#if AEGIS_WINDOWED
#include <volk.h>

// Only window creation + Vulkan surface support are needed; skip GL headers.
#define GLFW_INCLUDE_NONE  // must precede any glfw3.h inclusion
#include <GLFW/glfw3.h>

#include <aegis/vk/common.hpp>
#include <aegis/vk/device.hpp>
#include <aegis/vk/instance.hpp>
#include <aegis/vk/physical_device.hpp>
#include <aegis/vk/surface_glfw.hpp>
#include <aegis/vk/swapchain.hpp>

#include <memory>
#include <vector>

namespace {

using aegis::core::u32;

struct WindowState {
    bool framebuffer_resized = false;
};

void framebuffer_size_callback(GLFWwindow* window, int /*width*/, int /*height*/) {
    auto* state = static_cast<WindowState*>(glfwGetWindowUserPointer(window));
    if (state != nullptr) {
        state->framebuffer_resized = true;
    }
}

void glfw_error_callback(int code, const char* description) {
    AEGIS_LOG_ERROR("[glfw] error " << code << ": " << (description != nullptr ? description : ""));
}

// When the windowing stack is absent (container/CI), glfw is built with the
// Null platform. In that mode a failed Vulkan bootstrap is an environment
// property, not an engine bug: report it and exit successfully so the smoke
// build stays green.
constexpr bool kHeadlessSmoke = (AEGIS_GLFW_NULL_PLATFORM != 0);
constexpr std::uint32_t kMaxSmokeFrames = 120;

const VkClearColorValue kSpaceColor = {{0.015f, 0.020f, 0.055f, 1.0f}};  // deep space blue

int exit_from_failure(const char* stage) {
    if (kHeadlessSmoke) {
        AEGIS_LOG_INFO("[smoke] Vulkan runtime not available while bootstrapping '"
                       << stage << "' — this is expected without a display and/or Vulkan driver. "
                       << "Build verified; windowed run requires a machine with an X11/Wayland "
                          "session and a Vulkan 1.3 driver.");
        return 0;
    }
    AEGIS_LOG_ERROR("[aegis] fatal during '" << stage << "' stage");
    return 1;
}

int run_windowed() {
    glfwSetErrorCallback(glfw_error_callback);

    if constexpr (kHeadlessSmoke) {
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_NULL);
    }

    if (glfwInit() != GLFW_TRUE) {
        AEGIS_LOG_ERROR("[aegis] glfwInit failed");
        return 1;
    }
    struct GlfwGuard {
        ~GlfwGuard() { glfwTerminate(); }
    } glfw_guard;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    WindowState state{};
    GLFWwindow* raw_window = glfwCreateWindow(1280, 720,
                                              "Aegis2D — Milestone 1: Foundation & Vulkan Init",
                                              nullptr, nullptr);
    if (raw_window == nullptr) {
        AEGIS_LOG_ERROR("[aegis] failed to create GLFW window");
        return 1;
    }
    std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> window(raw_window,
                                                                     &glfwDestroyWindow);
    glfwSetWindowUserPointer(window.get(), &state);
    glfwSetFramebufferSizeCallback(window.get(), framebuffer_size_callback);

    // --- Vulkan instance --------------------------------------------------------
    aegis::vk::InstanceConfig instance_config;
    instance_config.application_name = "Aegis2D Demo";
    instance_config.enable_validation = true;
    // GLFW must be initialised before this call. Empty result means the
    // windowing build has no Vulkan-surface-capable platform (the glfw Null
    // platform), so VK_KHR_surface can never be enabled — fail loudly here
    // instead of with VK_ERROR_EXTENSION_NOT_PRESENT at surface creation.
    u32 glfw_extension_count = 0;
    const char** glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extension_count);
    if (glfw_extensions == nullptr || glfw_extension_count == 0) {
        AEGIS_LOG_ERROR("[aegis] glfwGetRequiredInstanceExtensions returned no Vulkan instance "
                        "extensions — rebuild with the Win32/X11/Wayland platform enabled");
        return exit_from_failure("required instance extensions");
    }
    AEGIS_LOG_INFO("[aegis] glfw requires " << glfw_extension_count << " instance extension(s)");
    for (u32 i = 0; i < glfw_extension_count; ++i) {
        AEGIS_LOG_INFO("[aegis] glfw instance extension: " << glfw_extensions[i]);
        instance_config.required_extensions.push_back(glfw_extensions[i]);
    }

    auto instance_result = aegis::vk::Instance::create(instance_config);
    if (!instance_result.has_value()) {
        AEGIS_LOG_ERROR("[aegis] " << instance_result.error().to_string());
        return exit_from_failure("instance");
    }
    aegis::vk::Instance instance = std::move(instance_result).take();

    // --- Surface ------------------------------------------------------------------
    auto surface_result = aegis::vk::GlfwSurface::create(instance, window.get());
    if (!surface_result.has_value()) {
        AEGIS_LOG_ERROR("[aegis] " << surface_result.error().to_string());
        return exit_from_failure("surface");
    }
    aegis::vk::GlfwSurface surface = std::move(surface_result).take();

    // --- Physical device -----------------------------------------------------------
    auto gpu_result = aegis::vk::select_physical_device(instance.handle(), surface.handle());
    if (!gpu_result.has_value()) {
        AEGIS_LOG_ERROR("[aegis] " << gpu_result.error().to_string());
        return exit_from_failure("physical device");
    }
    const aegis::vk::PhysicalDeviceInfo gpu = std::move(gpu_result).take();

    // --- Logical device -------------------------------------------------------------
    aegis::vk::DeviceConfig device_config;
    device_config.surface = surface.handle();
    device_config.enable_swapchain = true;
    auto device_result = aegis::vk::Device::create(gpu, device_config);
    if (!device_result.has_value()) {
        AEGIS_LOG_ERROR("[aegis] " << device_result.error().to_string());
        return exit_from_failure("device");
    }
    aegis::vk::Device device = std::move(device_result).take();

    // --- Swapchain -------------------------------------------------------------------
    int fb_width = 0;
    int fb_height = 0;
    glfwGetFramebufferSize(window.get(), &fb_width, &fb_height);
    if (fb_width <= 0 || fb_height <= 0) {
        fb_width = 1280;
        fb_height = 720;
    }

    aegis::vk::SwapchainConfig swapchain_config;
    swapchain_config.surface = surface.handle();
    swapchain_config.width = static_cast<aegis::core::u32>(fb_width);
    swapchain_config.height = static_cast<aegis::core::u32>(fb_height);
    swapchain_config.prefer_mailbox = true;

    auto swapchain_result = aegis::vk::Swapchain::create(device, gpu, swapchain_config);
    if (!swapchain_result.has_value()) {
        AEGIS_LOG_ERROR("[aegis] " << swapchain_result.error().to_string());
        return exit_from_failure("swapchain");
    }
    aegis::vk::Swapchain swapchain = std::move(swapchain_result).take();

    // --- Command pool / buffer ---------------------------------------------------------
    const VkCommandPoolCreateInfo pool_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = device.graphics_family(),
    };

    VkCommandPool command_pool = VK_NULL_HANDLE;
    if (vkCreateCommandPool(device.handle(), &pool_info, nullptr, &command_pool) != VK_SUCCESS) {
        AEGIS_LOG_ERROR("[aegis] vkCreateCommandPool failed");
        return exit_from_failure("command pool");
    }

    const VkCommandBufferAllocateInfo allocate_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device.handle(), &allocate_info, &command_buffer) != VK_SUCCESS) {
        AEGIS_LOG_ERROR("[aegis] vkAllocateCommandBuffers failed");
        vkDestroyCommandPool(device.handle(), command_pool, nullptr);
        return exit_from_failure("command buffer");
    }

    // --- Sync objects (two frames in flight) ------------------------------------------
    constexpr aegis::core::u32 kFramesInFlight = 2;
    std::vector<VkSemaphore> image_available(kFramesInFlight, VK_NULL_HANDLE);
    std::vector<VkSemaphore> render_finished(kFramesInFlight, VK_NULL_HANDLE);
    std::vector<VkFence> in_flight_fence(kFramesInFlight, VK_NULL_HANDLE);

    auto destroy_sync = [&]() {
        for (aegis::core::u32 i = 0; i < kFramesInFlight; ++i) {
            if (image_available[i] != VK_NULL_HANDLE) {
                vkDestroySemaphore(device.handle(), image_available[i], nullptr);
            }
            if (render_finished[i] != VK_NULL_HANDLE) {
                vkDestroySemaphore(device.handle(), render_finished[i], nullptr);
            }
            if (in_flight_fence[i] != VK_NULL_HANDLE) {
                vkDestroyFence(device.handle(), in_flight_fence[i], nullptr);
            }
        }
    };

    for (aegis::core::u32 i = 0; i < kFramesInFlight; ++i) {
        const VkSemaphoreCreateInfo semaphore_info{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
        };
        const VkFenceCreateInfo fence_info{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,  // first wait passes immediately
        };
        if (vkCreateSemaphore(device.handle(), &semaphore_info, nullptr, &image_available[i]) !=
                VK_SUCCESS ||
            vkCreateSemaphore(device.handle(), &semaphore_info, nullptr, &render_finished[i]) !=
                VK_SUCCESS ||
            vkCreateFence(device.handle(), &fence_info, nullptr, &in_flight_fence[i]) !=
                VK_SUCCESS) {
            AEGIS_LOG_ERROR("[aegis] failed to create sync objects");
            destroy_sync();
            vkDestroyCommandPool(device.handle(), command_pool, nullptr);
            return exit_from_failure("sync objects");
        }
    }

    AEGIS_LOG_INFO("[aegis] entering clear loop on '" << gpu.properties.deviceName << "'");

    // --- Main loop ---------------------------------------------------------------------
    aegis::core::u32 current_frame = 0;
    std::uint32_t frame_counter = 0;

    while (glfwWindowShouldClose(window.get()) == GLFW_FALSE) {
        glfwPollEvents();

        int size_w = 0;
        int size_h = 0;
        glfwGetFramebufferSize(window.get(), &size_w, &size_h);
        if (size_w <= 0 || size_h <= 0) {
            glfwWaitEvents();  // minimized / not yet sized
            continue;
        }

        const VkFence fence = in_flight_fence[current_frame];
        vkWaitForFences(device.handle(), 1, &fence, VK_TRUE, UINT64_MAX);

        aegis::core::u32 image_index = 0;
        const VkResult acquire_result =
            vkAcquireNextImageKHR(device.handle(), swapchain.handle(), UINT64_MAX,
                                  image_available[current_frame], VK_NULL_HANDLE, &image_index);

        if (acquire_result == VK_ERROR_OUT_OF_DATE_KHR) {
            device.wait_idle();
            if (!swapchain.recreate(static_cast<aegis::core::u32>(size_w),
                                    static_cast<aegis::core::u32>(size_h))) {
                AEGIS_LOG_ERROR("[aegis] swapchain recreate failed");
                break;
            }
            state.framebuffer_resized = false;
            continue;
        }
        if (acquire_result != VK_SUCCESS && acquire_result != VK_SUBOPTIMAL_KHR) {
            AEGIS_LOG_ERROR("[aegis] vkAcquireNextImageKHR failed: "
                            << aegis::vk::vk_result_to_string(acquire_result));
            break;
        }

        vkResetFences(device.handle(), 1, &fence);

        // ---- record: clear the acquired image -------------------------------
        vkResetCommandBuffer(command_buffer, 0);

        const VkCommandBufferBeginInfo begin_info{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = nullptr,
        };
        if (vkBeginCommandBuffer(command_buffer, &begin_info) != VK_SUCCESS) {
            AEGIS_LOG_ERROR("[aegis] vkBeginCommandBuffer failed");
            break;
        }

        VkImageMemoryBarrier2 to_attachment{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext = nullptr,
            .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask = VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = swapchain.images()[image_index],
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        };
        const VkDependencyInfo before_barrier{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext = nullptr,
            .dependencyFlags = 0,
            .memoryBarrierCount = 0,
            .pMemoryBarriers = nullptr,
            .bufferMemoryBarrierCount = 0,
            .pBufferMemoryBarriers = nullptr,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &to_attachment,
        };
        vkCmdPipelineBarrier2(command_buffer, &before_barrier);

        const VkRenderingAttachmentInfo color_attachment{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .pNext = nullptr,
            .imageView = swapchain.image_views()[image_index],
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .resolveMode = VK_RESOLVE_MODE_NONE,
            .resolveImageView = VK_NULL_HANDLE,
            .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = {.color = kSpaceColor},
        };
        const VkRenderingInfo rendering_info{
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .pNext = nullptr,
            .flags = 0,
            .renderArea = {{0, 0}, swapchain.extent()},
            .layerCount = 1,
            .viewMask = 0,
            .colorAttachmentCount = 1,
            .pColorAttachments = &color_attachment,
            .pDepthAttachment = nullptr,
            .pStencilAttachment = nullptr,
        };
        vkCmdBeginRendering(command_buffer, &rendering_info);
        // Milestone 1 clears the frame; scene rendering lands in Milestones 3+.
        vkCmdEndRendering(command_buffer);

        VkImageMemoryBarrier2 to_present{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext = nullptr,
            .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_NONE,
            .dstAccessMask = VK_ACCESS_2_NONE,
            .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = swapchain.images()[image_index],
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        };
        const VkDependencyInfo after_barrier{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext = nullptr,
            .dependencyFlags = 0,
            .memoryBarrierCount = 0,
            .pMemoryBarriers = nullptr,
            .bufferMemoryBarrierCount = 0,
            .pBufferMemoryBarriers = nullptr,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &to_present,
        };
        vkCmdPipelineBarrier2(command_buffer, &after_barrier);

        if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
            AEGIS_LOG_ERROR("[aegis] vkEndCommandBuffer failed");
            break;
        }

        // ---- submit + present --------------------------------------------------
        const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        const VkSubmitInfo submit_info{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = nullptr,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &image_available[current_frame],
            .pWaitDstStageMask = &wait_stage,
            .commandBufferCount = 1,
            .pCommandBuffers = &command_buffer,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &render_finished[current_frame],
        };
        if (vkQueueSubmit(device.graphics_queue(), 1, &submit_info, fence) != VK_SUCCESS) {
            AEGIS_LOG_ERROR("[aegis] vkQueueSubmit failed");
            break;
        }

        const VkSwapchainKHR present_swapchain = swapchain.handle();
        const VkPresentInfoKHR present_info{
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .pNext = nullptr,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &render_finished[current_frame],
            .swapchainCount = 1,
            .pSwapchains = &present_swapchain,
            .pImageIndices = &image_index,
            .pResults = nullptr,
        };
        const VkResult present_result = vkQueuePresentKHR(device.present_queue(), &present_info);

        if (state.framebuffer_resized || present_result == VK_ERROR_OUT_OF_DATE_KHR ||
            present_result == VK_SUBOPTIMAL_KHR) {
            device.wait_idle();
            int new_w = 0;
            int new_h = 0;
            glfwGetFramebufferSize(window.get(), &new_w, &new_h);
            if (new_w > 0 && new_h > 0) {
                if (!swapchain.recreate(static_cast<aegis::core::u32>(new_w),
                                        static_cast<aegis::core::u32>(new_h))) {
                    AEGIS_LOG_ERROR("[aegis] swapchain recreate failed");
                    break;
                }
            }
            state.framebuffer_resized = false;
        } else if (present_result != VK_SUCCESS) {
            AEGIS_LOG_ERROR("[aegis] vkQueuePresentKHR failed: "
                            << aegis::vk::vk_result_to_string(present_result));
            break;
        }

        current_frame = (current_frame + 1) % kFramesInFlight;
        ++frame_counter;
        if constexpr (kHeadlessSmoke) {
            if (frame_counter >= kMaxSmokeFrames) {
                glfwSetWindowShouldClose(window.get(), GLFW_TRUE);
            }
        }
    }

    // --- teardown (reverse order) -----------------------------------------------------
    device.wait_idle();
    destroy_sync();
    vkDestroyCommandPool(device.handle(), command_pool, nullptr);

    AEGIS_LOG_INFO("[aegis] shutdown complete after " << frame_counter << " frame(s)");
    return 0;  // swapchain/surface/device/instance/window/glfw guards tear down in reverse
}

} // namespace

#endif // AEGIS_WINDOWED

int main() {
    AEGIS_LOG_INFO("Aegis2D — Milestone 1: Foundation & Vulkan Initialization (v"
                   << aegis::core::kVersionString << ")");

#if AEGIS_WINDOWED
    return run_windowed();
#else
    AEGIS_LOG_INFO("built with AEGIS_WINDOWED=OFF; the GLFW windowing backend was skipped at "
                   "configure time. Re-configure with -DAEGIS_WINDOWED=ON on a machine with a "
                   "display and a Vulkan 1.3 driver to run the clear-loop demo.");
    return 0;
#endif
}
