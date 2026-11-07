// Aegis2D — aegis_game_check: headless Milestone-9 verification tool.
//
// Renders the built-in game scenes (no window, no surface, no swapchain):
//   1. aegis_game_vab.png    — the VAB with the SPARROW HOPPER preset loaded
//   2. aegis_game_flight.png — t = 2.0 s after liftoff, plumes + HUD live
//   3. aegis_game_map.png    — orbital map overlay on the same t = 2.0 s state
//
// Determinism: sessions step a fixed number of 1/60 s frames from fixed
// initial state; the VAB frame uses a fixed virtual wall clock. Repeated runs
// produce identical frames on the same driver.
//
// Usage:
//   aegis_game_check [output_prefix] [flight_seconds]
//     output_prefix  — file prefix, "" (default) writes the names above in the CWD
//     flight_seconds — simulated time before the flight/map shots (default 2.0)
//
// Exit codes: 0 on success OR when no Vulkan loader/driver is available
// (SKIP — CI-safe, like aegis_rocket_dump); 1 on a real failure.
#include <aegis/core/log.hpp>
#include <aegis/core/types.hpp>
#include <aegis/core/version.hpp>
#include <aegis/vk/device.hpp>
#include <aegis/vk/instance.hpp>
#include <aegis/vk/physical_device.hpp>
#include <aegis/renderer2d/batch_renderer.hpp>
#include <aegis/offscreen/offscreen_renderer.hpp>
#include <aegis/offscreen/png_writer.hpp>
#include <aegis/ui/ui_input.hpp>

#include "game_session.hpp"
#include "scene.hpp"

#include <cstring>
#include <string>
#include <stdexcept>

using namespace aegis;

namespace {

constexpr core::u32 kWidth = 1280;
constexpr core::u32 kHeight = 720;
constexpr core::f64 kDefaultFlightSeconds = 2.0;  ///< simulated time before the flight/map shots
constexpr core::f64 kWallClockSeconds = 12.5;  ///< fixed VAB clock (drives pulse animations)

void draw_vab_frame(renderer2d::BatchRenderer& renderer, game::SceneState& state,
                    const aegis::ui::UiInput& input, game::GameSession& session, core::f64 clock) {
    game::draw_vab(renderer, state, input, session, clock);
}

void draw_flight_frame(renderer2d::BatchRenderer& renderer, game::SceneState& state,
                       const aegis::ui::UiInput& input, game::GameSession& session, core::f64 /*clock*/) {
    game::draw_flight(renderer, state, input, session);
}

}  // namespace

int main(int argc, char** argv) {
    const std::string prefix = argc > 1 ? std::string(argv[1]) + "_" : "";
    const core::f64 flight_seconds = argc > 2 ? std::stod(argv[2]) : kDefaultFlightSeconds;
    AEGIS_LOG_INFO("Aegis2D — Milestone 9: aegis_game_check (v" << aegis::core::kVersionString << ")");

    // --- Headless Vulkan bootstrap (no window, no surface, no swapchain) -------
    vk::InstanceConfig instance_config;
    instance_config.application_name = "Aegis Game Check";
    instance_config.enable_validation = false;
    auto instance_result = vk::Instance::create(instance_config);
    if (!instance_result.has_value()) {
        AEGIS_LOG_INFO("[game_check] SKIP: no Vulkan instance available ("
                       << instance_result.error().message << ")");
        return 0;
    }
    vk::Instance instance = std::move(instance_result).take();

    auto gpu_result = vk::select_physical_device(instance.handle(), VK_NULL_HANDLE);  // headless
    if (!gpu_result.has_value()) {
        AEGIS_LOG_INFO("[game_check] SKIP: no Vulkan 1.3 device available ("
                       << gpu_result.error().message << ")");
        return 0;
    }
    const vk::PhysicalDeviceInfo gpu = std::move(gpu_result).take();

    vk::DeviceConfig device_config;
    device_config.surface = VK_NULL_HANDLE;
    device_config.enable_swapchain = false;
    auto device_result = vk::Device::create(gpu, device_config);
    if (!device_result.has_value()) {
        AEGIS_LOG_INFO("[game_check] SKIP: logical device creation failed ("
                       << device_result.error().message << ")");
        return 0;
    }
    vk::Device device = std::move(device_result).take();

    offscreen::OffscreenRendererConfig offscreen_config;
    offscreen_config.width = kWidth;
    offscreen_config.height = kHeight;
    offscreen_config.max_instances = 8192;
    offscreen_config.clear_color = {0.045f, 0.05f, 0.075f, 1.0f};  // VAB hangar tone
    auto offscreen_result = offscreen::OffscreenRenderer::create(
        device.handle(), gpu.memory_properties, device.graphics_queue(), device.graphics_family(),
        offscreen_config);
    if (!offscreen_result.has_value()) {
        AEGIS_LOG_ERROR("[game_check] offscreen renderer failed: "
                        << offscreen_result.error().message);
        return 1;
    }
    offscreen::OffscreenRenderer offscreen = std::move(offscreen_result).take();

    aegis::ui::UiInput input{};
    input.dt = static_cast<core::f64>(1.0 / 60.0);
    input.mouse_x = 640.0f;
    input.mouse_y = 430.0f;

    int failures = 0;
    const struct {
        const char* suffix;
        void (*draw)(renderer2d::BatchRenderer&, game::SceneState&, const aegis::ui::UiInput&,
                     game::GameSession&, core::f64);
        bool flight;
        bool map;
    } frames[] = {
        {"vab", draw_vab_frame, false, false},
        {"flight", draw_flight_frame, true, false},
        {"map", draw_flight_frame, true, true},
    };

    for (const auto& frame : frames) {
        game::GameSession session;
        if (frame.flight) {
            if (!session.launch()) {
                AEGIS_LOG_ERROR("[game_check] demo rocket failed to launch — blocker: "
                                << session.launch_blocker());
                return 1;
            }
            const core::u32 steps = static_cast<core::u32>(flight_seconds * 60.0);
            for (core::u32 i = 0; i < steps; ++i) {
                session.update(1.0 / 60.0);
            }
            if (frame.map) {
                session.toggle_map_overlay();
            }
        }

        game::SceneState state{};
        game::scene_state_init(state);
        game::scene_set_viewport(state, static_cast<core::f32>(kWidth), static_cast<core::f32>(kHeight));

        const std::string path = prefix + "aegis_game_" + frame.suffix + ".png";
        // Warm-up pass: render once against a throwaway capture so rig seeding
        // and easing settle before the projection is sampled for the kept frame.
        if (frame.flight) {
            const renderer2d::Camera2D& warmup_camera = game::scene_active_camera(state, session);
            (void)offscreen.capture(warmup_camera.view_projection(), [&](renderer2d::BatchRenderer& renderer) {
                frame.draw(renderer, state, input, session, kWallClockSeconds);
            });
        }
        const renderer2d::Camera2D& camera = game::scene_active_camera(state, session);
        const auto projection = camera.view_projection();

        auto pixels_result = offscreen.capture(projection, [&](renderer2d::BatchRenderer& renderer) {
            frame.draw(renderer, state, input, session, kWallClockSeconds);
        });
        if (!pixels_result.has_value()) {
            AEGIS_LOG_ERROR("[game_check] capture failed for " << path << ": "
                                                               << pixels_result.error().message);
            ++failures;
            continue;
        }
        auto png_result =
            offscreen::write_png(path.c_str(), kWidth, kHeight, pixels_result.value().data());
        if (!png_result.has_value()) {
            AEGIS_LOG_ERROR("[game_check] PNG write failed for " << path << ": "
                                                                 << png_result.error().message);
            ++failures;
            continue;
        }
        AEGIS_LOG_INFO("[game_check] GAME-CHECK/" << (frame.map ? "MAP" : (frame.flight ? "FLIGHT" : "VAB"))
                                                  << "_PNG: " << path);
    }

    if (failures != 0) {
        AEGIS_LOG_ERROR("[game_check] " << failures << " frame(s) failed");
        return 1;
    }
    AEGIS_LOG_INFO("[game_check] GAME/CHECK_VERIFIED");
    return 0;
}
