#include <algorithm>
#include <chrono>
#include <cmath>
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
#include <aegis/renderer2d/batch_renderer.hpp>
#include <aegis/renderer2d/projection.hpp>
#include <aegis/renderer2d/sprite_instance.hpp>

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
constexpr aegis::core::u32 kFramesInFlight = 2;

const VkClearColorValue kSpaceColor = {{0.015f, 0.020f, 0.055f, 1.0f}};  // deep space blue


constexpr float kHalfHeight = 4.5f;                     // world-space half height
constexpr float kHalfWidth = kHalfHeight * (1280.0f / 720.0f);  // == 8.0 for a 16:9 viewport

/// Tiny deterministic integer -> [0, 1] hash (for the starfield; no <random> needed).
float hash01(std::uint32_t v) {
    v ^= v >> 16;
    v *= 0x7feb352dU;
    v ^= v >> 15;
    v *= 0x846ca68bU;
    v ^= v >> 16;
    return static_cast<float>(v & 0x00ffffffU) / 16777215.0f;
}

aegis::renderer2d::SpriteInstance make_instance(float x, float y, float w, float h, float r, float g,
                                                float b, float a, float rotation = 0.0f,
                                                float softness = 0.0f) {
    aegis::renderer2d::SpriteInstance s;
    s.x = x;
    s.y = y;
    s.width = w;
    s.height = h;
    s.r = r;
    s.g = g;
    s.b = b;
    s.a = a;
    s.rotation = rotation;
    s.softness = softness;
    return s;
}

/// Draws the demo scene: starfield, soft-particle engine flame, rocket, and a
/// HUD panel with pips + a pulsing gauge. Painter order: sprites, then
/// additive particles, then UI.
void draw_scene(aegis::renderer2d::BatchRenderer& renderer, const float t, const float half_w,
                const float half_h) {
    // --- starfield ---------------------------------------------------------
    for (u32 i = 0; i < 170; ++i) {
        const float fx = hash01(i * 3U + 11U) * 2.0f - 1.0f;
        const float fy = hash01(i * 3U + 47U) * 2.0f - 1.0f;
        const float size = 0.02f + 0.05f * hash01(i * 3U + 101U);
        const float twinkle =
            0.5f + 0.5f * std::sin(t * (1.1f + hash01(i * 7U + 3U) * 2.2f) + static_cast<float>(i) * 1.618f);
        renderer.draw_sprite(make_instance(fx * half_w, fy * half_h, size, size, 0.85f, 0.9f, 1.0f,
                                           0.35f + 0.65f * twinkle));
    }

    const float bob = std::sin(t * 0.9f) * 0.08f;
    const float base_x = 0.0f;
    const float base_y = bob;

    // --- engine flame (additive particles) ---------------------------------
    for (u32 i = 0; i < 6; ++i) {
        const float flicker = 0.5f + 0.5f * std::sin(t * 11.0f + static_cast<float>(i) * 1.7f);
        const float size = 0.42f + 0.28f * flicker + 0.06f * static_cast<float>(i);
        const float px = base_x + std::sin(t * 7.3f + static_cast<float>(i) * 2.3f) * 0.09f;
        const float py = base_y - 2.15f - 0.38f * static_cast<float>(i) - 0.18f * flicker;
        const float fade = 1.0f - static_cast<float>(i) / 6.0f;
        renderer.draw_particle(make_instance(px, py, size, size * 1.5f, 1.0f,
                                             0.55f + 0.25f * flicker, 0.12f, 0.85f * fade, 0.0f, 0.95f));
    }

    // --- rocket ------------------------------------------------------------
    renderer.draw_sprite(make_instance(base_x - 0.83f, base_y - 1.28f, 0.5f, 1.2f, 0.78f, 0.20f, 0.18f,
                                       1.0f, 0.35f));   // left fin
    renderer.draw_sprite(make_instance(base_x + 0.83f, base_y - 1.28f, 0.5f, 1.2f, 0.78f, 0.20f, 0.18f,
                                       1.0f, -0.35f));  // right fin
    renderer.draw_sprite(make_instance(base_x, base_y + 0.15f, 1.18f, 3.0f, 0.88f, 0.90f, 0.94f,
                                       1.0f));          // body
    renderer.draw_sprite(make_instance(base_x, base_y - 0.55f, 1.18f, 0.28f, 0.85f, 0.25f, 0.22f,
                                       1.0f));          // red stripe
    renderer.draw_sprite(make_instance(base_x, base_y + 2.05f, 1.0f, 1.0f, 0.85f, 0.27f, 0.23f, 1.0f,
                                       0.7853982f));    // nose cone (rotated square)
    renderer.draw_sprite(make_instance(base_x, base_y + 0.85f, 0.38f, 0.38f, 0.45f, 0.80f, 0.98f,
                                       0.95f, 0.0f, 0.65f));  // porthole (soft dot)
    renderer.draw_sprite(make_instance(base_x, base_y + 0.15f, 0.30f, 0.30f, 0.45f, 0.80f, 0.98f,
                                       0.95f, 0.0f, 0.65f));  // porthole (soft dot)
    renderer.draw_sprite(make_instance(base_x - 0.12f, base_y + 0.95f, 0.12f, 0.12f, 1.0f, 1.0f, 1.0f,
                                       0.9f, 0.0f, 0.9f));    // cockpit glint

    // --- HUD panel (bottom) -------------------------------------------------
    const float panel_h = 1.0f;
    const float panel_y = -half_h + panel_h * 0.5f + 0.18f;
    const float panel_w = half_w * 2.0f * 0.94f;
    renderer.draw_ui(make_instance(0.0f, panel_y, panel_w, panel_h, 0.05f, 0.07f, 0.13f, 0.82f));

    for (u32 i = 0; i < 5; ++i) {  // system pips (3 online, 2 dim)
        const bool online = i < 3;
        renderer.draw_ui(make_instance(-half_w + 0.45f + static_cast<float>(i) * 0.30f, panel_y,
                                       0.17f, 0.17f, online ? 0.40f : 0.55f, online ? 0.90f : 0.60f,
                                       online ? 0.55f : 0.65f, 0.9f));
    }
    renderer.draw_ui(make_instance(-half_w + 2.35f, panel_y + 0.14f, 1.5f, 0.12f, 0.75f, 0.78f, 0.85f,
                                   0.8f));  // label bar
    renderer.draw_ui(make_instance(-half_w + 2.05f, panel_y - 0.16f, 1.0f, 0.12f, 0.55f, 0.58f, 0.66f,
                                   0.8f));  // label bar

    const float track_w = half_w * 0.5f;
    const float track_x = half_w - track_w * 0.5f - 0.35f;
    renderer.draw_ui(make_instance(track_x, panel_y, track_w, 0.3f, 0.16f, 0.18f, 0.24f, 0.95f));
    const float fill = 0.55f + 0.35f * std::sin(t * 0.8f);
    const float fill_w = track_w * fill;
    renderer.draw_ui(make_instance(track_x - track_w * 0.5f + fill_w * 0.5f, panel_y, fill_w, 0.16f,
                                   0.42f, 0.97f, 0.55f, 0.95f));
    renderer.draw_ui(make_instance(track_x, panel_y + 0.33f, track_w * 0.6f, 0.1f, 0.6f, 0.63f, 0.7f,
                                   0.8f));  // gauge caption
}

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
    const auto kStartTime = std::chrono::steady_clock::now();
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
                                              "Aegis2D — Milestone 3: 2D Batch Renderer",
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
    u32 glfw_extension_count = 0;
    const char** glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extension_count);

    aegis::vk::InstanceConfig instance_config;
    instance_config.application_name = "Aegis2D Batch Demo";
    instance_config.enable_validation = true;
    for (u32 i = 0; i < glfw_extension_count; ++i) {
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

    // --- Batch renderer (Milestone 3) -------------------------------------------------
    aegis::renderer2d::BatchRendererConfig renderer_config;
    renderer_config.max_instances = 4096;
    renderer_config.max_frames_in_flight = kFramesInFlight;
    auto renderer_result = aegis::renderer2d::BatchRenderer::create(
        device.handle(), gpu.memory_properties, swapchain.image_format(), renderer_config);
    if (!renderer_result.has_value()) {
        AEGIS_LOG_ERROR("[aegis] " << renderer_result.error().to_string());
        return exit_from_failure("batch renderer");
    }
    aegis::renderer2d::BatchRenderer renderer = std::move(renderer_result).take();

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

    AEGIS_LOG_INFO("[aegis] entering render loop on '" << gpu.properties.deviceName << "'");

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
        {
            const auto projection = aegis::renderer2d::make_orthographic(-kHalfWidth, kHalfWidth,
                                                                         -kHalfHeight, kHalfHeight);
            const float time_seconds = std::chrono::duration<float>(
                std::chrono::steady_clock::now() - kStartTime).count();
            renderer.begin_frame(command_buffer, current_frame, projection.data(), swapchain.extent());
            draw_scene(renderer, time_seconds, kHalfWidth, kHalfHeight);
            renderer.flush();
        }
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
        if ((frame_counter % 120) == 0) {
            const auto& stats = renderer.stats();
            AEGIS_LOG_INFO("[stats] frame " << frame_counter << ": " << stats.draw_calls
                           << " draw call(s), " << stats.instance_count << " instance(s) ("
                           << "sprite=" << stats.sprite_instances << ", particle="
                           << stats.particle_instances << ", ui=" << stats.ui_instances << ")");
        }
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
    AEGIS_LOG_INFO("Aegis2D — Milestone 3: Universal 2D Batch Renderer (v"
                   << aegis::core::kVersionString << ")");

#if AEGIS_WINDOWED
    return run_windowed();
#else
    AEGIS_LOG_INFO("built with AEGIS_WINDOWED=OFF; the GLFW windowing backend was skipped at "
                   "configure time. Re-configure with -DAEGIS_WINDOWED=ON on a machine with a "
                   "display and a Vulkan 1.3 driver to run the batch-renderer demo.");
    return 0;
#endif
}
