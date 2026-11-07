// aegis_orbit_demo — Milestone 4 demo: 2D zoom camera + orbital trajectory
// (orbit path polyline, velocity vector) for a sun/planet two-body system.
//
// Controls:
//   scroll            zoom anchored at the cursor
//   W/A/S/D / arrows  pan (any manual pan cancels follow mode)
//   F                 toggle camera follow (locked to the planet)
//   R                 reset camera (sun-centered, default zoom)
//   Esc               quit
//
// Windowed only — never registered with ctest. In driverless/headless
// environments (glfw Null platform) the demo exits 0 with a smoke message.
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
#include <aegis/renderer2d/camera.hpp>
#include <aegis/renderer2d/projection.hpp>
#include <aegis/renderer2d/sprite_instance.hpp>
#include <aegis/orbit/orbit.hpp>

#include <memory>
#include <vector>

namespace {

using aegis::core::u32;

constexpr bool kHeadlessSmoke = (AEGIS_GLFW_NULL_PLATFORM != 0);
constexpr std::uint32_t kMaxSmokeFrames = 120;
constexpr aegis::core::u32 kFramesInFlight = 2;

const VkClearColorValue kSpaceColor = {{0.012f, 0.016f, 0.046f, 1.0f}};  // deep space blue

// --- Demo orbital system ----------------------------------------------------
// Units are "world units" (1 wu = 1 pixel at zoom 1). A sun/planet two-body
// system: planet starts at periapsis at t = 0.
constexpr double kOrbitA = 430.0;      // semi-major axis (wu)
constexpr double kOrbitE = 0.28;       // eccentricity
constexpr double kOrbitOmega = 0.4363; // periapsis angle, radians (~25°)
constexpr double kOrbitPeriod = 26.0;  // seconds
constexpr double kMu = 39.4784176043574 * (kOrbitA * kOrbitA * kOrbitA) / (kOrbitPeriod * kOrbitPeriod);
constexpr double kPeriapsisRadius = kOrbitA * (1.0 - kOrbitE);
constexpr double kApoapsisRadius = kOrbitA * (1.0 + kOrbitE);
constexpr double kSpeedPeriapsis =
    std::sqrt(kMu * (2.0 / kPeriapsisRadius - 1.0 / kOrbitA));

aegis::orbit::Elements demo_elements() {
    aegis::orbit::Elements elements;
    elements.semi_major_axis = kOrbitA;
    elements.eccentricity = kOrbitE;
    elements.periapsis_angle = kOrbitOmega;
    elements.gravitational_parameter = kMu;
    elements.periapsis_time = 0.0;
    return elements;
}

struct WindowState {
    bool framebuffer_resized = false;
    bool f_prev = false;
    bool r_prev = false;
    aegis::renderer2d::Camera2D* camera = nullptr;
};

void framebuffer_size_callback(GLFWwindow* window, int /*width*/, int /*height*/) {
    auto* state = static_cast<WindowState*>(glfwGetWindowUserPointer(window));
    if (state != nullptr) {
        state->framebuffer_resized = true;
    }
}

void scroll_callback(GLFWwindow* window, double /*xoffset*/, double yoffset) {
    auto* state = static_cast<WindowState*>(glfwGetWindowUserPointer(window));
    if (state == nullptr || state->camera == nullptr) {
        return;
    }
    double cursor_x = 0.0;
    double cursor_y = 0.0;
    glfwGetCursorPos(window, &cursor_x, &cursor_y);
    const float factor = std::pow(1.12f, static_cast<float>(yoffset));
    state->camera->zoom_at(static_cast<float>(cursor_x), static_cast<float>(cursor_y), factor);
}

void glfw_error_callback(int code, const char* description) {
    AEGIS_LOG_ERROR("[glfw] error " << code << ": " << (description != nullptr ? description : ""));
}

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

/// Pixel-anchored UI rect (screen-space HUD element): px origin = top-left.
void draw_ui_rect(aegis::renderer2d::BatchRenderer& renderer,
                  const aegis::renderer2d::Camera2D& camera,
                  float px_x, float px_y, float px_w, float px_h,
                  float r, float g, float b, float a) {
    const float wpp = camera.world_per_pixel();
    const auto bottom_left = camera.screen_to_world(px_x, px_y + px_h);
    const auto top_right = camera.screen_to_world(px_x + px_w, px_y);
    const float cx = (bottom_left[0] + top_right[0]) * 0.5f;
    const float cy = (bottom_left[1] + top_right[1]) * 0.5f;
    renderer.draw_ui(make_instance(cx, cy, px_w * wpp, px_h * wpp, r, g, b, a));
}

/// Draws the solar system: starfield, sun, orbit path, periapsis/apoapsis
/// markers, planet, velocity vector, pixel-anchored HUD.
void draw_scene(aegis::renderer2d::BatchRenderer& renderer,
                const aegis::renderer2d::Camera2D& camera,
                const float sim_time,
                const aegis::orbit::State& planet,
                const std::vector<std::array<float, 2>>& orbit_path) {
    const float wpp = camera.world_per_pixel();
    const auto& pos = planet.position;

    // --- starfield (world-fixed; spread over a large region) -----------------
    for (u32 i = 0; i < 260; ++i) {
        const float sx = hash01(i * 5U + 17U) * 7000.0f - 3500.0f;
        const float sy = hash01(i * 5U + 91U) * 7000.0f - 3500.0f;
        const float size = 1.2f + 3.4f * hash01(i * 5U + 131U);
        const float twinkle =
            0.45f + 0.55f * std::sin(sim_time * (0.9f + hash01(i * 7U + 3U) * 2.4f) +
                                      static_cast<float>(i) * 1.618f);
        renderer.draw_sprite(make_instance(sx, sy, size, size, 0.85f, 0.9f, 1.0f,
                                           0.3f + 0.65f * twinkle));
    }

    // --- sun (additive particles: glow, then bright core) --------------------
    renderer.draw_particle(make_instance(0.0f, 0.0f, 340.0f, 340.0f, 1.0f, 0.72f, 0.22f,
                                         0.75f, 0.0f, 0.95f));
    renderer.draw_particle(make_instance(0.0f, 0.0f, 150.0f, 150.0f, 1.0f, 0.85f, 0.38f,
                                         0.9f, 0.0f, 0.9f));
    renderer.draw_sprite(make_instance(0.0f, 0.0f, 78.0f, 78.0f, 1.0f, 0.93f, 0.62f, 1.0f));

    // --- orbit path (specialized line pipeline; soft cyan polyline) ----------
    constexpr float kOrbitColorR = 0.46f, kOrbitColorG = 0.66f, kOrbitColorB = 1.0f;
    const float orbit_thickness = 2.4f * wpp;   // ~2.4 px on screen at any zoom
    for (std::size_t i = 0; i + 1 < orbit_path.size(); ++i) {
        renderer.draw_line(orbit_path[i][0], orbit_path[i][1], orbit_path[i + 1][0], orbit_path[i + 1][1],
                           kOrbitColorR, kOrbitColorG, kOrbitColorB, 0.55f, orbit_thickness, 0.3f);
    }
    if (orbit_path.size() >= 2) {  // close the loop
        renderer.draw_line(orbit_path.back()[0], orbit_path.back()[1], orbit_path.front()[0],
                           orbit_path.front()[1], kOrbitColorR, kOrbitColorG, kOrbitColorB, 0.55f,
                           orbit_thickness, 0.3f);
    }

    // --- periapsis / apoapsis markers -----------------------------------------
    const float cos_w = std::cos(static_cast<float>(kOrbitOmega));
    const float sin_w = std::sin(static_cast<float>(kOrbitOmega));
    renderer.draw_sprite(make_instance(static_cast<float>(kPeriapsisRadius) * cos_w,
                                       static_cast<float>(kPeriapsisRadius) * sin_w,
                                       9.0f, 9.0f, 1.0f, 0.85f, 0.35f, 0.9f, 0.0f, 0.7f));
    renderer.draw_sprite(make_instance(-static_cast<float>(kApoapsisRadius) * cos_w,
                                       -static_cast<float>(kApoapsisRadius) * sin_w,
                                       9.0f, 9.0f, 0.75f, 0.6f, 0.95f, 0.9f, 0.0f, 0.7f));

    // --- planet ---------------------------------------------------------------
    const float planet_x = static_cast<float>(pos[0]);
    const float planet_y = static_cast<float>(pos[1]);
    renderer.draw_particle(make_instance(planet_x, planet_y, 64.0f, 64.0f, 0.55f, 0.72f, 1.0f,
                                         0.5f, 0.0f, 0.9f));
    renderer.draw_sprite(make_instance(planet_x, planet_y, 26.0f, 26.0f, 0.72f, 0.82f, 1.0f,
                                       1.0f));

    // --- velocity vector (line + arrowhead) ------------------------------------
    const float speed_ratio = static_cast<float>(planet.speed / kSpeedPeriapsis);
    const float vector_len = 150.0f * speed_ratio;  // 150 wu at periapsis
    const float inv_speed = planet.speed > 1e-6 ? static_cast<float>(1.0 / planet.speed) : 0.0f;
    const float dir_x = static_cast<float>(planet.velocity[0]) * inv_speed;
    const float dir_y = static_cast<float>(planet.velocity[1]) * inv_speed;
    const float tip_x = planet_x + dir_x * vector_len;
    const float tip_y = planet_y + dir_y * vector_len;
    const float line_thickness = 3.2f * wpp;
    renderer.draw_line(planet_x, planet_y, tip_x, tip_y, 0.36f, 1.0f, 0.72f, 0.95f,
                       line_thickness, 0.12f);

    const float arrow_len = vector_len * 0.22f;
    const float arrow_angle = 0.46f;  // ~26°
    const float base_angle = std::atan2(dir_y, dir_x);
    for (const float delta : {arrow_angle, -arrow_angle}) {
        const float a = base_angle + 3.14159265f + delta;  // pointing back along the vector
        renderer.draw_line(tip_x, tip_y,
                           tip_x + std::cos(a) * arrow_len, tip_y + std::sin(a) * arrow_len,
                           0.36f, 1.0f, 0.72f, 0.95f, line_thickness, 0.12f);
    }

    // --- HUD (pixel-anchored): panel + speed and radius bars -------------------
    const float viewport_h = camera.viewport_height();
    const float panel_x = 18.0f;
    const float panel_y = viewport_h - 96.0f;
    const float panel_w = 392.0f;
    const float panel_h = 78.0f;
    draw_ui_rect(renderer, camera, panel_x - 2.0f, panel_y - 2.0f, panel_w + 4.0f, panel_h + 4.0f,
                 0.32f, 0.40f, 0.56f, 0.55f);   // border
    draw_ui_rect(renderer, camera, panel_x, panel_y, panel_w, panel_h, 0.05f, 0.07f, 0.13f, 0.82f);

    const float track_x = panel_x + 18.0f;
    const float track_w = 236.0f;
    const float track_h = 10.0f;

    // speed bar
    const float speed_bar_y = panel_y + 14.0f;
    draw_ui_rect(renderer, camera, track_x, speed_bar_y, track_w, track_h, 0.14f, 0.16f, 0.22f, 0.95f);
    draw_ui_rect(renderer, camera, track_x, speed_bar_y, track_w * std::clamp(speed_ratio, 0.0f, 1.0f),
                 track_h, 0.35f, 0.95f, 0.62f, 0.98f);
    // radius bar
    const float radius_ratio = static_cast<float>(
        (planet.radius - kPeriapsisRadius) / (kApoapsisRadius - kPeriapsisRadius));
    const float radius_bar_y = panel_y + 42.0f;
    draw_ui_rect(renderer, camera, track_x, radius_bar_y, track_w, track_h, 0.14f, 0.16f, 0.22f, 0.95f);
    draw_ui_rect(renderer, camera, track_x, radius_bar_y, track_w * std::clamp(radius_ratio, 0.0f, 1.0f),
                 track_h, 1.0f, 0.78f, 0.32f, 0.98f);
    // caption pips (speed / radius)
    draw_ui_rect(renderer, camera, track_x + track_w + 10.0f, speed_bar_y + 1.0f, 8.0f, track_h - 2.0f,
                 0.35f, 0.95f, 0.62f, 0.98f);
    draw_ui_rect(renderer, camera, track_x + track_w + 10.0f, radius_bar_y + 1.0f, 8.0f, track_h - 2.0f,
                 1.0f, 0.78f, 0.32f, 0.98f);
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
    auto last_frame_time = kStartTime;
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
                                              "Aegis2D — Milestone 4: Orbital Trajectory",
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

    aegis::renderer2d::Camera2DConfig camera_config;
    camera_config.viewport_width = 1280.0f;
    camera_config.viewport_height = 720.0f;
    camera_config.initial_zoom = 0.9f;   // the full orbit fits with margin
    camera_config.min_zoom = 0.06f;
    camera_config.max_zoom = 32.0f;
    aegis::renderer2d::Camera2D camera(camera_config);
    state.camera = &camera;

    const aegis::orbit::Elements elements = demo_elements();
    const std::vector<std::array<float, 2>> orbit_path =
        aegis::orbit::sample_path(elements, 240);

    // --- Vulkan instance --------------------------------------------------------
    u32 glfw_extension_count = 0;
    const char** glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extension_count);

    aegis::vk::InstanceConfig instance_config;
    instance_config.application_name = "Aegis2D Orbit Demo";
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
    camera.set_viewport(static_cast<float>(fb_width), static_cast<float>(fb_height));

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

    // --- Batch renderer ---------------------------------------------------------------
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

    bool follow_planet = true;
    AEGIS_LOG_INFO("[aegis] entering render loop on '" << gpu.properties.deviceName << "'");

    // --- Main loop ---------------------------------------------------------------------
    aegis::core::u32 current_frame = 0;
    std::uint32_t frame_counter = 0;

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
            glfwWaitEvents();  // minimized / not yet sized
            continue;
        }
        camera.set_viewport(static_cast<float>(size_w), static_cast<float>(size_h));

        // --- input: follow/reset toggles + pan ---------------------------------------
        const float sim_time = std::chrono::duration<float>(frame_start - kStartTime).count();
        const auto planet = aegis::orbit::state_at(elements, static_cast<double>(sim_time));

        const bool f_down = glfwGetKey(window.get(), GLFW_KEY_F) == GLFW_PRESS;
        if (f_down && !state.f_prev) {
            follow_planet = !follow_planet;
            AEGIS_LOG_INFO("[orbit] follow mode " << (follow_planet ? "ON (camera locked to planet)" : "OFF"));
        }
        state.f_prev = f_down;

        const bool r_down = glfwGetKey(window.get(), GLFW_KEY_R) == GLFW_PRESS;
        if (r_down && !state.r_prev) {
            camera.set_position(0.0f, 0.0f);
            camera.set_zoom(0.9f);
            follow_planet = false;
            AEGIS_LOG_INFO("[orbit] camera reset (sun-centered)");
        }
        state.r_prev = r_down;

        if (glfwGetKey(window.get(), GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window.get(), GLFW_TRUE);
        }

        const bool pan_left = glfwGetKey(window.get(), GLFW_KEY_A) == GLFW_PRESS ||
                              glfwGetKey(window.get(), GLFW_KEY_LEFT) == GLFW_PRESS;
        const bool pan_right = glfwGetKey(window.get(), GLFW_KEY_D) == GLFW_PRESS ||
                               glfwGetKey(window.get(), GLFW_KEY_RIGHT) == GLFW_PRESS;
        const bool pan_up = glfwGetKey(window.get(), GLFW_KEY_W) == GLFW_PRESS ||
                            glfwGetKey(window.get(), GLFW_KEY_UP) == GLFW_PRESS;
        const bool pan_down = glfwGetKey(window.get(), GLFW_KEY_S) == GLFW_PRESS ||
                              glfwGetKey(window.get(), GLFW_KEY_DOWN) == GLFW_PRESS;
        if (pan_left || pan_right || pan_up || pan_down) {
            follow_planet = false;  // manual pan cancels follow
            const float pan_world = 520.0f * camera.world_per_pixel() * dt;  // ~520 px/s feel
            if (pan_left) {
                camera.move(-pan_world, 0.0f);
            }
            if (pan_right) {
                camera.move(pan_world, 0.0f);
            }
            if (pan_up) {
                camera.move(0.0f, pan_world);
            }
            if (pan_down) {
                camera.move(0.0f, -pan_world);
            }
        }
        if (follow_planet) {
            camera.set_position(static_cast<float>(planet.position[0]),
                                static_cast<float>(planet.position[1]));
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
            const auto projection = camera.view_projection();
            renderer.begin_frame(command_buffer, current_frame, projection.data(), swapchain.extent());
            draw_scene(renderer, camera, sim_time, planet, orbit_path);
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
                camera.set_viewport(static_cast<float>(new_w), static_cast<float>(new_h));
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
            const double true_anomaly_deg = planet.true_anomaly * 57.29577951308232;
            AEGIS_LOG_INFO("[stats] frame " << frame_counter << ": " << stats.draw_calls
                           << " draw call(s), " << stats.instance_count << " instance(s) ("
                           << "sprite=" << stats.sprite_instances << ", particle="
                           << stats.particle_instances << ", ui=" << stats.ui_instances
                           << ", line=" << stats.line_instances << ") | r=" << planet.radius
                           << " speed=" << planet.speed << " theta=" << true_anomaly_deg
                           << "deg | cam=(" << camera.x() << ", " << camera.y()
                           << ") zoom=" << camera.zoom());
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
    AEGIS_LOG_INFO("Aegis2D — Milestone 4: Camera & Orbital Trajectory System (v"
                   << aegis::core::kVersionString << ")");
    AEGIS_LOG_INFO("controls: scroll = zoom (cursor-anchored) | WASD/arrows = pan | F = follow | R = reset | Esc = quit");

#if AEGIS_WINDOWED
    return run_windowed();
#else
    AEGIS_LOG_INFO("built with AEGIS_WINDOWED=OFF; the GLFW windowing backend was skipped at "
                   "configure time. Re-configure with -DAEGIS_WINDOWED=ON on a machine with a "
                   "display and a Vulkan 1.3 driver to run the orbit demo.");
    return 0;
#endif
}
