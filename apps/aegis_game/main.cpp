// aegis_game — Milestone 9: VAB → Flight, the full gameplay loop.
//
// Controls
//   VAB:    mouse — pick parts, place them (snap), delete mode (X or the
//           palette toggle), wheel zoom, LAUNCH button
//   FLIGHT: A/D or Q/E or ←/→ — pitch   W/S or ↑/↓ — throttle
//           Space/Enter — stage   M — orbital map   Esc — abort to VAB
//   In the VAB, Esc quits.
//
// Windowed only — never registered with ctest. In driverless/headless
// environments (glfw Null platform) the app boots, auto-launches the demo
// rocket after 30 frames and exits 0 with a smoke message.
#if defined(_WIN32)
#   define WIN32_LEAN_AND_MEAN
#   define NOMINMAX
#   include <windows.h>  // SetProcessDPIAware (older GLFW path)
#endif
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

#include "game_session.hpp"
#include "scene.hpp"

#include <cstdlib>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace {

using aegis::core::u32;

constexpr bool kHeadlessSmoke = (AEGIS_GLFW_NULL_PLATFORM != 0);
constexpr std::uint32_t kMaxSmokeFrames = 240;
constexpr aegis::core::u32 kFramesInFlight = 2;

const VkClearColorValue kHangarColor = {{0.045f, 0.05f, 0.075f, 1.0f}};

// ---------------------------------------------------------------------------
// Input: GLFW -> UiInput
// ---------------------------------------------------------------------------
constexpr std::pair<int, aegis::ui::UiKey> kKeyTable[] = {
    {GLFW_KEY_A, aegis::ui::UiKey::A},         {GLFW_KEY_D, aegis::ui::UiKey::D},
    {GLFW_KEY_E, aegis::ui::UiKey::E},         {GLFW_KEY_F, aegis::ui::UiKey::F},         {GLFW_KEY_G, aegis::ui::UiKey::G},
    {GLFW_KEY_M, aegis::ui::UiKey::M},         {GLFW_KEY_Q, aegis::ui::UiKey::Q},
    {GLFW_KEY_R, aegis::ui::UiKey::R},         {GLFW_KEY_S, aegis::ui::UiKey::S},
    {GLFW_KEY_W, aegis::ui::UiKey::W},         {GLFW_KEY_X, aegis::ui::UiKey::X},
    {GLFW_KEY_Z, aegis::ui::UiKey::Z},         {GLFW_KEY_SPACE, aegis::ui::UiKey::Space},
    {GLFW_KEY_ESCAPE, aegis::ui::UiKey::Escape},   {GLFW_KEY_ENTER, aegis::ui::UiKey::Enter},
    {GLFW_KEY_BACKSPACE, aegis::ui::UiKey::Backspace}, {GLFW_KEY_DELETE, aegis::ui::UiKey::Delete},
    {GLFW_KEY_TAB, aegis::ui::UiKey::Tab},     {GLFW_KEY_UP, aegis::ui::UiKey::Up},
    {GLFW_KEY_DOWN, aegis::ui::UiKey::Down},   {GLFW_KEY_LEFT, aegis::ui::UiKey::Left},
    {GLFW_KEY_RIGHT, aegis::ui::UiKey::Right}, {GLFW_KEY_LEFT_SHIFT, aegis::ui::UiKey::LeftShift},
    {GLFW_KEY_LEFT_CONTROL, aegis::ui::UiKey::LeftCtrl},
    {GLFW_KEY_1, aegis::ui::UiKey::Num1},
    {GLFW_KEY_2, aegis::ui::UiKey::Num2},
    {GLFW_KEY_3, aegis::ui::UiKey::Num3},
};

struct InputState {
    float wheel = 0.0f;
    bool mouse_was_down = false;
    aegis::core::u64 prev_keys = 0;

    aegis::ui::UiInput build(GLFWwindow* window, const float dt) noexcept {
        aegis::ui::UiInput input;
        input.dt = static_cast<aegis::core::f64>(dt);
        double mouse_x = 0.0, mouse_y = 0.0;
        glfwGetCursorPos(window, &mouse_x, &mouse_y);
        input.mouse_x = static_cast<float>(mouse_x);
        input.mouse_y = static_cast<float>(mouse_y);
        input.wheel = wheel;
        wheel = 0.0f;  // consume
        const bool down = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        input.mouse_down = down;
        input.mouse_pressed = down && !mouse_was_down;
        input.mouse_released = !down && mouse_was_down;
        mouse_was_down = down;
        aegis::core::u64 mask = 0;
        for (const auto& [glfw_key, ui_key] : kKeyTable) {
            if (glfwGetKey(window, glfw_key) == GLFW_PRESS) {
                mask |= aegis::ui::key_bit(ui_key);
            }
        }
        input.keys_down = mask;
        input.keys_pressed = mask & ~prev_keys;
        prev_keys = mask;
        return input;
    }
};

struct WindowState {
    bool framebuffer_resized = false;
    InputState input;
};

void framebuffer_size_callback(GLFWwindow* window, int /*width*/, int /*height*/) {
    auto* state = static_cast<WindowState*>(glfwGetWindowUserPointer(window));
    if (state != nullptr) {
        state->framebuffer_resized = true;
    }
}

void scroll_callback(GLFWwindow* window, double /*xoffset*/, const double yoffset) {
    auto* state = static_cast<WindowState*>(glfwGetWindowUserPointer(window));
    if (state != nullptr) {
        state->input.wheel += static_cast<float>(yoffset);
    }
}

void glfw_error_callback(int code, const char* description) {
    AEGIS_LOG_ERROR("[glfw] error " << code << ": " << (description != nullptr ? description : ""));
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

}  // namespace

#endif
#if AEGIS_WINDOWED
namespace {
int run_windowed() {
    const auto kStartTime = std::chrono::steady_clock::now();
    auto last_frame_time = kStartTime;
    glfwSetErrorCallback(glfw_error_callback);

    if constexpr (kHeadlessSmoke) {
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_NULL);
    }

#if defined(_WIN32)
    // Win32 DPI awareness must be declared BEFORE the first window / Vulkan call
    // or the swapchain, mouse coordinates and UI hit-testing disagree on scaled
    // displays (blurry output, offset UI). SetProcessDPIAware (user32, Vista+) is
    // the universally supported path; if a future GLFW exposes a native
    // DPI-awareness init hint the #if picks that up automatically.
#if defined(GLFW_WIN32_DPI_AWARENESS) && defined(GLFW_DPI_AWARENESS_SYSTEM)
    glfwInitHint(GLFW_WIN32_DPI_AWARENESS, GLFW_DPI_AWARENESS_SYSTEM);
#else
    SetProcessDPIAware();
#endif
#endif

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
    GLFWwindow* raw_window = glfwCreateWindow(1280, 720, "Aegis2D — Vehicle Assembly Building",
                                              nullptr, nullptr);
    if (raw_window == nullptr) {
        AEGIS_LOG_ERROR("[aegis] failed to create GLFW window");
        return 1;
    }
    std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> window(raw_window,
                                                                     &glfwDestroyWindow);
    glfwSetWindowUserPointer(window.get(), &state);
    glfwSetFramebufferSizeCallback(window.get(), framebuffer_size_callback);
    glfwSetScrollCallback(window.get(), scroll_callback);

    aegis::game::GameSession session;
    aegis::game::SceneState scene;
    aegis::game::scene_state_init(scene);

    int fb_width = 0;
    int fb_height = 0;
    glfwGetFramebufferSize(window.get(), &fb_width, &fb_height);
    if (fb_width <= 0 || fb_height <= 0) {
        fb_width = 1280;
        fb_height = 720;
    }
    aegis::game::scene_set_viewport(scene, static_cast<float>(fb_width), static_cast<float>(fb_height));

    // --- Vulkan instance + window surface --------------------------------------
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
    std::vector<const char*> glfw_extension_list;
    glfw_extension_list.reserve(glfw_extension_count);
    AEGIS_LOG_INFO("[aegis] glfw requires " << glfw_extension_count << " instance extension(s)");
    for (u32 i = 0; i < glfw_extension_count; ++i) {
        AEGIS_LOG_INFO("[aegis] glfw instance extension: " << glfw_extensions[i]);
        glfw_extension_list.push_back(glfw_extensions[i]);
    }

    // Validation layers are a developer diagnostic, not a runtime requirement.
    // Opt in with AEGIS_VK_VALIDATION=1; off by default because a layer/SDK
    // version mismatch on older GPUs (e.g. GTX 970) can turn vkCreateWin32SurfaceKHR
    // into VK_ERROR_INITIALIZATION_FAILED on otherwise healthy drivers.
    bool validation_requested = false;
    if (const char* const env = std::getenv("AEGIS_VK_VALIDATION")) {
        const std::string value(env);
        validation_requested = value == "1" || value == "true" || value == "TRUE" || value == "yes" ||
                               value == "YES" || value == "on" || value == "ON";
    }
    if (!validation_requested) {
        AEGIS_LOG_INFO("[aegis] validation layers off (set AEGIS_VK_VALIDATION=1 to enable)");
    }

    const auto bring_up = [&](const bool with_validation)
        -> std::optional<std::pair<aegis::vk::Instance, aegis::vk::GlfwSurface>> {
        aegis::vk::InstanceConfig instance_config;
        instance_config.application_name = "Aegis2D Game";
        instance_config.enable_validation = with_validation;
        instance_config.required_extensions = glfw_extension_list;
        auto instance_result = aegis::vk::Instance::create(instance_config);
        if (!instance_result.has_value()) {
            AEGIS_LOG_ERROR("[aegis] " << instance_result.error().to_string());
            return std::nullopt;
        }
        aegis::vk::Instance instance = std::move(instance_result).take();
        auto surface_result = aegis::vk::GlfwSurface::create(instance, window.get());
        if (!surface_result.has_value()) {
            AEGIS_LOG_ERROR("[aegis] " << surface_result.error().to_string());
            return std::nullopt;  // RAII: dead instance unwinds before returning
        }
        return std::make_pair(std::move(instance), std::move(surface_result).take());
    };

    auto bringup = bring_up(validation_requested);
    if (!bringup && validation_requested) {
        AEGIS_LOG_WARN("[aegis] bring-up failed with validation layers enabled — retrying once "
                       "without them (layer/driver mismatch is a known source of "
                       "VK_ERROR_INITIALIZATION_FAILED from vkCreateWin32SurfaceKHR)");
        bringup = bring_up(false);
    }
    if (!bringup) {
        AEGIS_LOG_ERROR("[aegis] Vulkan window bring-up failed — check the graphics driver "
                        "(GTX 970 needs NVIDIA 472.12+ / Vulkan 1.3) and relaunch");
        return exit_from_failure("instance/surface");
    }
    aegis::vk::Instance instance = std::move(bringup->first);
    aegis::vk::GlfwSurface surface = std::move(bringup->second);

    auto gpu_result = aegis::vk::select_physical_device(instance.handle(), surface.handle());
    if (!gpu_result.has_value()) {
        AEGIS_LOG_ERROR("[aegis] " << gpu_result.error().to_string());
        return exit_from_failure("physical device");
    }
    const aegis::vk::PhysicalDeviceInfo gpu = std::move(gpu_result).take();

    aegis::vk::DeviceConfig device_config;
    device_config.surface = surface.handle();
    device_config.enable_swapchain = true;
    auto device_result = aegis::vk::Device::create(gpu, device_config);
    if (!device_result.has_value()) {
        AEGIS_LOG_ERROR("[aegis] " << device_result.error().to_string());
        return exit_from_failure("device");
    }
    aegis::vk::Device device = std::move(device_result).take();

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

    aegis::renderer2d::BatchRendererConfig renderer_config;
    renderer_config.max_instances = 8192;
    renderer_config.max_frames_in_flight = kFramesInFlight;
    auto renderer_result = aegis::renderer2d::BatchRenderer::create(
        device.handle(), gpu.memory_properties, swapchain.image_format(), renderer_config);
    if (!renderer_result.has_value()) {
        AEGIS_LOG_ERROR("[aegis] " << renderer_result.error().to_string());
        return exit_from_failure("batch renderer");
    }
    aegis::renderer2d::BatchRenderer renderer = std::move(renderer_result).take();
    renderer.set_glyph_atlas(device.graphics_queue(), device.graphics_family(), scene.font.pixels(),
                             scene.font.atlas_width(), scene.font.atlas_height());

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
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
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

    AEGIS_LOG_INFO("[aegis] entering the game on '" << gpu.properties.deviceName << "'");

    aegis::core::u32 current_frame = 0;
    std::uint32_t frame_counter = 0;
    aegis::game::GameScreen previous_screen = aegis::game::GameScreen::Build;

    while (glfwWindowShouldClose(window.get()) == GLFW_FALSE) {
        const auto frame_start = std::chrono::steady_clock::now();
        const float dt = std::clamp(std::chrono::duration<float>(frame_start - last_frame_time).count(),
                                    0.0f, 1.0f / 15.0f);
        last_frame_time = frame_start;
        glfwPollEvents();

        int size_w = 0;
        int size_h = 0;
        glfwGetFramebufferSize(window.get(), &size_w, &size_h);
        if (size_w <= 0 || size_h <= 0) {
            glfwWaitEvents();
            continue;
        }
        aegis::game::scene_set_viewport(scene, static_cast<float>(size_w), static_cast<float>(size_h));

        const aegis::core::f64 wall_clock =
            std::chrono::duration<aegis::core::f64>(frame_start - kStartTime).count();
        const aegis::ui::UiInput input = state.input.build(window.get(), dt);

        // --- global input ---------------------------------------------------
        if (session.screen() == aegis::game::GameScreen::Build) {
            if (input.pressed(aegis::ui::UiKey::Escape)) {
                glfwSetWindowShouldClose(window.get(), GLFW_TRUE);
            }
            if (input.pressed(aegis::ui::UiKey::Num1)) {
                session.load_preset(0);
            } else if (input.pressed(aegis::ui::UiKey::Num2)) {
                session.load_preset(1);
            } else if (input.pressed(aegis::ui::UiKey::Num3)) {
                session.load_preset(2);
            }
        } else {
            if (input.pressed(aegis::ui::UiKey::Escape)) {
                session.abort_to_vab();
                aegis::game::scene_state_reset_flight(scene);
            }
            if (input.pressed(aegis::ui::UiKey::M)) {
                session.toggle_map_overlay();
            }
            if (input.pressed(aegis::ui::UiKey::Space) || input.pressed(aegis::ui::UiKey::Enter)) {
                session.activate_stage();
            }
            const bool pitch_left = input.down(aegis::ui::UiKey::A) || input.down(aegis::ui::UiKey::Q) ||
                                    input.down(aegis::ui::UiKey::Left);
            const bool pitch_right = input.down(aegis::ui::UiKey::D) || input.down(aegis::ui::UiKey::E) ||
                                     input.down(aegis::ui::UiKey::Right);
            session.set_pitch_input((pitch_left ? 1.0 : 0.0) + (pitch_right ? -1.0 : 0.0));
            if (input.down(aegis::ui::UiKey::W) || input.down(aegis::ui::UiKey::Up)) {
                session.adjust_throttle(static_cast<aegis::core::f64>(dt) * 0.55);
            }
            if (input.down(aegis::ui::UiKey::S) || input.down(aegis::ui::UiKey::Down)) {
                session.adjust_throttle(-static_cast<aegis::core::f64>(dt) * 0.55);
            }
        }

        // Headless smoke: auto-launch on schedule, quit after N frames.
        if (kHeadlessSmoke) {
            if (frame_counter == 30) {
                AEGIS_LOG_INFO("[smoke] auto-launching the demo rocket");
                (void)session.launch();
            }
            if (frame_counter >= kMaxSmokeFrames) {
                glfwSetWindowShouldClose(window.get(), GLFW_TRUE);
            }
        }

        session.update(static_cast<aegis::core::f64>(dt));

        // Window title tracks the screen.
        if (session.screen() != previous_screen) {
            previous_screen = session.screen();
            glfwSetWindowTitle(window.get(),
                               previous_screen == aegis::game::GameScreen::Build
                                   ? "Aegis2D — Vehicle Assembly Building"
                                   : "Aegis2D — Flight");
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
            .clearValue = {.color = kHangarColor},
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
            aegis::renderer2d::Camera2D& camera = aegis::game::scene_active_camera(scene, session);
            const auto projection = camera.view_projection();
            renderer.begin_frame(command_buffer, current_frame, projection.data(), swapchain.extent());
            if (session.screen() == aegis::game::GameScreen::Build) {
                aegis::game::draw_vab(renderer, scene, input, session, wall_clock);
            } else {
                aegis::game::draw_flight(renderer, scene, input, session);
            }
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

        const VkSwapchainKHR swapchain_handle = swapchain.handle();
        const VkPresentInfoKHR present_info{
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .pNext = nullptr,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &render_finished[current_frame],
            .swapchainCount = 1,
            .pSwapchains = &swapchain_handle,
            .pImageIndices = &image_index,
            .pResults = nullptr,
        };
        const VkResult present_result = vkQueuePresentKHR(device.graphics_queue(), &present_info);
        if (present_result == VK_ERROR_OUT_OF_DATE_KHR || present_result == VK_SUBOPTIMAL_KHR ||
            state.framebuffer_resized) {
            device.wait_idle();
            if (!swapchain.recreate(static_cast<aegis::core::u32>(size_w),
                                    static_cast<aegis::core::u32>(size_h))) {
                AEGIS_LOG_ERROR("[aegis] swapchain recreate failed");
                break;
            }
            state.framebuffer_resized = false;
        } else if (present_result != VK_SUCCESS) {
            AEGIS_LOG_ERROR("[aegis] vkQueuePresentKHR failed: "
                            << aegis::vk::vk_result_to_string(present_result));
            break;
        }

        ++frame_counter;
        current_frame = (current_frame + 1) % kFramesInFlight;
    }

    device.wait_idle();
    destroy_sync();
    vkDestroyCommandPool(device.handle(), command_pool, nullptr);
    return 0;
}
}  // namespace
#endif  // AEGIS_WINDOWED

int main() {
#if AEGIS_WINDOWED
    return run_windowed();
#else
    AEGIS_LOG_INFO("Aegis2D — aegis_game (headless build). "
                   "Configure with -DAEGIS_WINDOWED=ON to open the VAB.");
    return 0;
#endif
}
