// aegis_rocket_dump — Milestone 5 tool: renders the deterministic rocket
// scene (a multi-part 2D rocket flying a Keplerian orbit around a sun, framed
// by the zoom camera) into an offscreen RGBA8 image and writes a PNG file.
//
// Determinism: the simulation time is a fixed constant (kSimTimeSeconds), all
// jitter/flicker values are derived from deterministic integer hashes and
// sines of it, so repeated runs produce identical pixels.
//
// Usage:
//   aegis_rocket_dump [output.png]     (default: rocket_frame.png in the CWD)
//
// Exit codes: 0 on success OR when no Vulkan loader/driver is available
// (SKIP — CI-safe, like aegis_roundtrip); 1 on a real failure.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <aegis/core/log.hpp>
#include <aegis/core/types.hpp>
#include <aegis/core/version.hpp>
#include <aegis/vk/device.hpp>
#include <aegis/vk/instance.hpp>
#include <aegis/vk/physical_device.hpp>
#include <aegis/renderer2d/batch_renderer.hpp>
#include <aegis/renderer2d/camera.hpp>
#include <aegis/renderer2d/sprite_instance.hpp>
#include <aegis/orbit/orbit.hpp>
#include <aegis/offscreen/offscreen_renderer.hpp>
#include <aegis/offscreen/png_writer.hpp>

namespace {

using aegis::core::u32;

// --- Fixed scenario ---------------------------------------------------------
constexpr double kSimTimeSeconds = 7.0;          // fixed for a deterministic frame
constexpr double kOrbitA = 430.0;                // semi-major axis (wu)
constexpr double kOrbitE = 0.28;                 // eccentricity
constexpr double kOrbitOmega = 0.4363;           // periapsis angle (~25°)
constexpr double kOrbitPeriod = 26.0;            // seconds
constexpr double kMu = 39.4784176043574 * (kOrbitA * kOrbitA * kOrbitA) / (kOrbitPeriod * kOrbitPeriod);
constexpr double kPeriapsisRadius = kOrbitA * (1.0 - kOrbitE);
constexpr double kApoapsisRadius = kOrbitA * (1.0 + kOrbitE);
constexpr double kSpeedPeriapsis =
    std::sqrt(kMu * (2.0 / kPeriapsisRadius - 1.0 / kOrbitA));

constexpr u32 kWidth = 1280;
constexpr u32 kHeight = 720;
constexpr float kPi = 3.14159265358979323846f;

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

/// Draws the deterministic scene. All randomness-like values derive from hash01
/// and sines of the fixed simulation time.
void draw_rocket_scene(aegis::renderer2d::BatchRenderer& renderer,
                       const aegis::renderer2d::Camera2D& camera,
                       const aegis::orbit::State& ship,
                       const std::vector<std::array<float, 2>>& orbit_path) {
    const float t = static_cast<float>(kSimTimeSeconds);
    const float wpp = camera.world_per_pixel();

    // --- starfield (fixed spread around the sun) -------------------------------
    for (u32 i = 0; i < 340; ++i) {
        const float sx = hash01(i * 5U + 17U) * 8000.0f - 4000.0f;
        const float sy = hash01(i * 5U + 91U) * 8000.0f - 4000.0f;
        const float size = 1.1f + 3.6f * hash01(i * 5U + 131U);
        const float twinkle =
            0.45f + 0.55f * std::sin(t * (0.9f + hash01(i * 7U + 3U) * 2.2f) + static_cast<float>(i) * 1.618f);
        renderer.draw_sprite(make_instance(sx, sy, size, size, 0.85f, 0.9f, 1.0f,
                                           0.3f + 0.65f * twinkle));
    }

    // --- sun (additive particles: glow, then bright core) ----------------------
    renderer.draw_particle(make_instance(0.0f, 0.0f, 340.0f, 340.0f, 1.0f, 0.72f, 0.22f,
                                         0.75f, 0.0f, 0.95f));
    renderer.draw_particle(make_instance(0.0f, 0.0f, 150.0f, 150.0f, 1.0f, 0.85f, 0.38f,
                                         0.9f, 0.0f, 0.9f));
    renderer.draw_sprite(make_instance(0.0f, 0.0f, 78.0f, 78.0f, 1.0f, 0.93f, 0.62f, 1.0f));

    // --- orbit path (specialized line pipeline) ---------------------------------
    const float orbit_thickness = 2.4f * wpp;
    for (std::size_t i = 0; i + 1 < orbit_path.size(); ++i) {
        renderer.draw_line(orbit_path[i][0], orbit_path[i][1], orbit_path[i + 1][0], orbit_path[i + 1][1],
                           0.46f, 0.66f, 1.0f, 0.5f, orbit_thickness, 0.3f);
    }
    if (orbit_path.size() >= 2) {
        renderer.draw_line(orbit_path.back()[0], orbit_path.back()[1], orbit_path.front()[0],
                           orbit_path.front()[1], 0.46f, 0.66f, 1.0f, 0.5f, orbit_thickness, 0.3f);
    }

    // --- periapsis / apoapsis markers -------------------------------------------
    const float cos_w = std::cos(static_cast<float>(kOrbitOmega));
    const float sin_w = std::sin(static_cast<float>(kOrbitOmega));
    renderer.draw_sprite(make_instance(static_cast<float>(kPeriapsisRadius) * cos_w,
                                       static_cast<float>(kPeriapsisRadius) * sin_w,
                                       9.0f, 9.0f, 1.0f, 0.85f, 0.35f, 0.9f, 0.0f, 0.7f));
    renderer.draw_sprite(make_instance(-static_cast<float>(kApoapsisRadius) * cos_w,
                                       -static_cast<float>(kApoapsisRadius) * sin_w,
                                       9.0f, 9.0f, 0.75f, 0.6f, 0.95f, 0.9f, 0.0f, 0.7f));

    // --- the rocket --------------------------------------------------------------
    const float px = static_cast<float>(ship.position[0]);
    const float py = static_cast<float>(ship.position[1]);
    const float heading = std::atan2(static_cast<float>(ship.velocity[1]),
                                     static_cast<float>(ship.velocity[0]));
    const float cosh = std::cos(heading);
    const float sinh = std::sin(heading);
    const auto ship_space = [&](const float lx, const float ly) {
        return std::pair<float, float>{px + lx * cosh - ly * sinh, py + lx * sinh + ly * cosh};
    };

    // exhaust particles (deterministic flicker)
    for (u32 i = 0; i < 7; ++i) {
        const float local_x = -2.05f - 0.34f * static_cast<float>(i);
        const float wiggle = std::sin(t * 13.0f + static_cast<float>(i) * 2.1f) * 0.14f;
        const float size = 0.55f + 0.22f * std::sin(t * 17.0f + static_cast<float>(i) * 1.6f) +
                           0.09f * static_cast<float>(i);
        const float fade = 1.0f - static_cast<float>(i) / 7.0f;
        const auto [ex, ey] = ship_space(local_x, wiggle);
        renderer.draw_particle(make_instance(ex, ey, size, size * 1.5f, 1.0f,
                                             0.55f + 0.25f * fade, 0.12f, 0.85f * fade,
                                             heading, 0.95f));
    }
    // engine nozzle glow
    const auto [nx, ny] = ship_space(-1.9f, 0.0f);
    renderer.draw_particle(make_instance(nx, ny, 1.1f, 1.1f, 1.0f, 0.75f, 0.3f, 0.9f,
                                         heading, 0.9f));

    // fins, body, stripe, nose cone, portholes, glint
    const auto [fin_lx, fin_ly] = ship_space(-1.25f, 1.0f);
    renderer.draw_sprite(make_instance(fin_lx, fin_ly, 1.15f, 0.5f, 0.78f, 0.20f, 0.18f, 1.0f,
                                       heading - 0.38f));
    const auto [fin_rx, fin_ry] = ship_space(-1.25f, -1.0f);
    renderer.draw_sprite(make_instance(fin_rx, fin_ry, 1.15f, 0.5f, 0.78f, 0.20f, 0.18f, 1.0f,
                                       heading + 0.38f));
    renderer.draw_sprite(make_instance(px, py, 3.1f, 1.18f, 0.88f, 0.90f, 0.94f, 1.0f, heading));
    const auto [stx, sty] = ship_space(0.62f, 0.0f);
    renderer.draw_sprite(make_instance(stx, sty, 0.34f, 1.18f, 0.85f, 0.25f, 0.22f, 1.0f, heading));
    const auto [nose_x, nose_y] = ship_space(2.15f, 0.0f);
    renderer.draw_sprite(make_instance(nose_x, nose_y, 1.05f, 1.05f, 0.85f, 0.27f, 0.23f, 1.0f,
                                       heading + 0.7853982f));
    const auto [p1x, p1y] = ship_space(0.42f, 0.0f);
    renderer.draw_sprite(make_instance(p1x, p1y, 0.38f, 0.38f, 0.45f, 0.80f, 0.98f, 0.95f,
                                       heading, 0.65f));
    const auto [p2x, p2y] = ship_space(1.12f, 0.0f);
    renderer.draw_sprite(make_instance(p2x, p2y, 0.38f, 0.38f, 0.45f, 0.80f, 0.98f, 0.95f,
                                       heading, 0.65f));
    const auto [gx, gy] = ship_space(1.16f, 0.16f);
    renderer.draw_sprite(make_instance(gx, gy, 0.12f, 0.12f, 1.0f, 1.0f, 1.0f, 0.9f,
                                       heading, 0.9f));

    // --- velocity vector (line + arrowhead) --------------------------------------
    const float speed_ratio = static_cast<float>(ship.speed / kSpeedPeriapsis);
    const float vector_len = 130.0f * speed_ratio;
    const float inv_speed = ship.speed > 1e-6 ? static_cast<float>(1.0 / ship.speed) : 0.0f;
    const float dir_x = static_cast<float>(ship.velocity[0]) * inv_speed;
    const float dir_y = static_cast<float>(ship.velocity[1]) * inv_speed;
    const float tip_x = px + dir_x * vector_len;
    const float tip_y = py + dir_y * vector_len;
    const float line_thickness = 3.0f * wpp;
    renderer.draw_line(px, py, tip_x, tip_y, 0.36f, 1.0f, 0.72f, 0.9f, line_thickness, 0.12f);
    const float arrow_len = vector_len * 0.22f;
    const float base_angle = std::atan2(dir_y, dir_x);
    for (const float delta : {0.46f, -0.46f}) {
        const float a = base_angle + kPi + delta;
        renderer.draw_line(tip_x, tip_y, tip_x + std::cos(a) * arrow_len,
                           tip_y + std::sin(a) * arrow_len, 0.36f, 1.0f, 0.72f, 0.9f,
                           line_thickness, 0.12f);
    }

    // --- HUD (pixel-anchored): border, panel, speed/radius bars, caption pips ------
    const float viewport_h = camera.viewport_height();
    const float panel_x = 18.0f;
    const float panel_y = viewport_h - 96.0f;
    const float panel_w = 392.0f;
    const float panel_h = 78.0f;
    draw_ui_rect(renderer, camera, panel_x - 2.0f, panel_y - 2.0f, panel_w + 4.0f, panel_h + 4.0f,
                 0.32f, 0.40f, 0.56f, 0.55f);
    draw_ui_rect(renderer, camera, panel_x, panel_y, panel_w, panel_h, 0.05f, 0.07f, 0.13f, 0.82f);

    const float track_x = panel_x + 18.0f;
    const float track_w = 236.0f;
    const float track_h = 10.0f;

    const float speed_bar_y = panel_y + 14.0f;
    draw_ui_rect(renderer, camera, track_x, speed_bar_y, track_w, track_h, 0.14f, 0.16f, 0.22f, 0.95f);
    draw_ui_rect(renderer, camera, track_x, speed_bar_y, track_w * std::clamp(speed_ratio, 0.0f, 1.0f),
                 track_h, 0.35f, 0.95f, 0.62f, 0.98f);

    const float radius_ratio = static_cast<float>(
        (ship.radius - kPeriapsisRadius) / (kApoapsisRadius - kPeriapsisRadius));
    const float radius_bar_y = panel_y + 42.0f;
    draw_ui_rect(renderer, camera, track_x, radius_bar_y, track_w, track_h, 0.14f, 0.16f, 0.22f, 0.95f);
    draw_ui_rect(renderer, camera, track_x, radius_bar_y, track_w * std::clamp(radius_ratio, 0.0f, 1.0f),
                 track_h, 1.0f, 0.78f, 0.32f, 0.98f);

    draw_ui_rect(renderer, camera, track_x + track_w + 10.0f, speed_bar_y + 1.0f, 8.0f, track_h - 2.0f,
                 0.35f, 0.95f, 0.62f, 0.98f);
    draw_ui_rect(renderer, camera, track_x + track_w + 10.0f, radius_bar_y + 1.0f, 8.0f, track_h - 2.0f,
                 1.0f, 0.78f, 0.32f, 0.98f);
}

aegis::orbit::Elements demo_elements() {
    aegis::orbit::Elements elements;
    elements.semi_major_axis = kOrbitA;
    elements.eccentricity = kOrbitE;
    elements.periapsis_angle = kOrbitOmega;
    elements.gravitational_parameter = kMu;
    elements.periapsis_time = 0.0;
    return elements;
}

int run(const char* output_path) {
    const aegis::orbit::Elements elements = demo_elements();
    const aegis::orbit::State ship = aegis::orbit::state_at(elements, kSimTimeSeconds);
    const std::vector<std::array<float, 2>> orbit_path = aegis::orbit::sample_path(elements, 180);

    // --- Camera: centered between the sun and the ship, zoomed to fit both -------
    const float ship_x = static_cast<float>(ship.position[0]);
    const float ship_y = static_cast<float>(ship.position[1]);
    const float cam_x = ship_x * 0.5f;
    const float cam_y = ship_y * 0.5f;
    const float sun_distance = std::hypot(cam_x, cam_y);
    const float ship_distance = std::hypot(ship_x - cam_x, ship_y - cam_y);
    const float half_needed = std::max(sun_distance + 140.0f, ship_distance + 80.0f);  // sun disc + HUD headroom
    const float zoom = 340.0f / half_needed;  // vertical fit dominates on 16:9

    aegis::renderer2d::Camera2DConfig camera_config;
    camera_config.viewport_width = static_cast<float>(kWidth);
    camera_config.viewport_height = static_cast<float>(kHeight);
    camera_config.initial_zoom = zoom;
    camera_config.min_zoom = 0.05f;
    camera_config.max_zoom = 32.0f;
    aegis::renderer2d::Camera2D camera(camera_config);
    camera.set_position(cam_x, cam_y);

    // --- Headless Vulkan bootstrap (no window, no surface, no swapchain) ---------
    aegis::vk::InstanceConfig instance_config;
    instance_config.application_name = "Aegis2D Rocket Dump";
    instance_config.enable_validation = true;

    auto instance_result = aegis::vk::Instance::create(instance_config);
    if (!instance_result.has_value()) {
        AEGIS_LOG_INFO("[rocket_dump] SKIP: no Vulkan instance available ("
                       << instance_result.error().message << ")");
        return 0;
    }
    aegis::vk::Instance instance = std::move(instance_result).take();

    auto gpu_result = aegis::vk::select_physical_device(instance.handle(), VK_NULL_HANDLE);  // headless
    if (!gpu_result.has_value()) {
        AEGIS_LOG_INFO("[rocket_dump] SKIP: no suitable GPU (" << gpu_result.error().message << ")");
        return 0;
    }
    const aegis::vk::PhysicalDeviceInfo gpu = std::move(gpu_result).take();

    aegis::vk::DeviceConfig device_config;
    device_config.surface = VK_NULL_HANDLE;
    device_config.enable_swapchain = false;
    auto device_result = aegis::vk::Device::create(gpu, device_config);
    if (!device_result.has_value()) {
        AEGIS_LOG_INFO("[rocket_dump] SKIP: logical device creation failed ("
                       << device_result.error().message << ")");
        return 0;
    }
    aegis::vk::Device device = std::move(device_result).take();

    aegis::offscreen::OffscreenRendererConfig offscreen_config;
    offscreen_config.width = kWidth;
    offscreen_config.height = kHeight;
    offscreen_config.max_instances = 8192;
    auto offscreen_result = aegis::offscreen::OffscreenRenderer::create(
        device.handle(), gpu.memory_properties, device.graphics_queue(), device.graphics_family(),
        offscreen_config);
    if (!offscreen_result.has_value()) {
        AEGIS_LOG_ERROR("[rocket_dump] offscreen renderer creation failed: "
                        << offscreen_result.error().to_string());
        return 1;
    }
    aegis::offscreen::OffscreenRenderer offscreen = std::move(offscreen_result).take();

    const auto projection = camera.view_projection();
    AEGIS_LOG_INFO("[rocket_dump] rendering deterministic frame (t = " << kSimTimeSeconds
                   << "s, ship r = " << ship.radius << ", cam zoom = " << zoom << ")");

    auto pixels_result = offscreen.capture(projection, [&](aegis::renderer2d::BatchRenderer& renderer) {
        draw_rocket_scene(renderer, camera, ship, orbit_path);
    });
    if (!pixels_result.has_value()) {
        AEGIS_LOG_ERROR("[rocket_dump] capture failed: " << pixels_result.error().to_string());
        return 1;
    }
    const std::vector<aegis::core::u8>& pixels = pixels_result.value();

    auto png_result = aegis::offscreen::write_png(output_path, kWidth, kHeight, pixels.data());
    if (!png_result.has_value()) {
        AEGIS_LOG_ERROR("[rocket_dump] PNG write failed: " << png_result.error().to_string());
        return 1;
    }

    AEGIS_LOG_INFO("[rocket_dump] ROCKET_FRAME_WRITTEN path=" << output_path << " (" << kWidth
                   << "x" << kHeight << ", " << pixels.size() << " pixel bytes)");
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    AEGIS_LOG_INFO("Aegis2D — Milestone 5: Test Scene & Offscreen Renderer (v"
                   << aegis::core::kVersionString << ")");

    if (argc > 2 || (argc == 2 && (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help"))) {
        AEGIS_LOG_INFO("usage: aegis_rocket_dump [output.png]  (default: rocket_frame.png)");
        return 0;
    }
    const char* output_path = argc == 2 ? argv[1] : "rocket_frame.png";
    return run(output_path);
}
