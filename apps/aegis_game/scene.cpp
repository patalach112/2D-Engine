// Aegis2D — scene rendering implementation (Milestone 9).
#include "scene.hpp"

#include <aegis/render/part_renderer.hpp>
#include <aegis/render/planet_renderer.hpp>
#include <aegis/render/ui_bridge.hpp>
#include <aegis/renderer2d/camera.hpp>
#include <aegis/renderer2d/sprite_instance.hpp>
#include <aegis/ui/font.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

using aegis::renderer2d::BatchRenderer;
using aegis::renderer2d::Camera2D;

namespace aegis::game {
namespace {

constexpr core::f64 kEarthMu = 3.986004418e14;
constexpr core::f64 kEarthRadius = 6.371e6;       ///< render rebase: world y' = y - R
constexpr core::f32 kPi = 3.14159265358979323846f;

// Palette short labels, indexed by TemplateId (registry order, 0..12).
const char* const kTemplateShortName[13] = {"POD",   "LONG-2", "LONG-4", "SHORT-1", "SPARR", "LNGBW", "KSTRL",
                                            "DECOP", "HVY-S",  "RAD-S",  "FAIR",    "NOSE",  "STRUT"};

struct VabLayout {
    aegis::ui::UiRect palette{8.0f, 46.0f, 210.0f, 470.0f};
    aegis::ui::UiRect info{1052.0f, 46.0f, 220.0f, 420.0f};
    aegis::ui::UiRect launch{455.0f, 644.0f, 370.0f, 58.0f};
};

bool point_in(const aegis::ui::UiInput& input, const aegis::ui::UiRect rect) noexcept {
    return input.mouse_x >= rect.x && input.mouse_x < rect.x + rect.w && input.mouse_y >= rect.y &&
           input.mouse_y < rect.y + rect.h;
}

void draw_fullscreen_bands(BatchRenderer& renderer, const Camera2D& camera,
                           const std::array<core::f32, 3> top, const std::array<core::f32, 3> bottom,
                           const int band_count) noexcept {
    const core::f32 vh = camera.visible_world_height();
    const core::f32 vw = camera.visible_world_width();
    const core::f32 top_y = camera.y() + vh * 0.5f;
    for (int i = 0; i < band_count; ++i) {
        const core::f32 t = (static_cast<core::f32>(i) + 0.5f) / static_cast<core::f32>(band_count);
        aegis::renderer2d::SpriteInstance band{};
        band.x = camera.x();
        band.y = top_y - (static_cast<core::f32>(i) + 0.5f) * (vh / static_cast<core::f32>(band_count));
        band.width = vw + 4.0f * camera.world_per_pixel();
        band.height = vh / static_cast<core::f32>(band_count) + 1.5f * camera.world_per_pixel();
        band.r = top[0] + (bottom[0] - top[0]) * t;
        band.g = top[1] + (bottom[1] - top[1]) * t;
        band.b = top[2] + (bottom[2] - top[2]) * t;
        band.a = 1.0f;
        renderer.draw_sprite(band);
    }
}

void draw_grid(BatchRenderer& renderer, const Camera2D& camera) noexcept {
    const core::f32 wpp = camera.world_per_pixel();
    const core::f32 thin = wpp * 1.4f;
    // 1-wu lines (faint), 5-wu majors, and the vertical build axis (x = 0).
    for (core::i32 x = -10; x <= 10; ++x) {
        const core::f32 fx = static_cast<core::f32>(x);
        const bool major = (x % 5) == 0;
        renderer.draw_line(fx, -0.5f, fx, 14.5f, 0.62f, 0.70f, 0.86f, major ? 0.22f : 0.09f, thin, 0.0f);
    }
    for (core::i32 y = 0; y <= 14; ++y) {
        const core::f32 fy = static_cast<core::f32>(y);
        const bool major = (y % 5) == 0;
        renderer.draw_line(-11.0f, fy, 11.0f, fy, 0.62f, 0.70f, 0.86f, major ? 0.20f : 0.08f, thin, 0.0f);
    }
    renderer.draw_line(0.5f, -0.5f, 0.5f, 14.5f, 1.0f, 0.62f, 0.25f, 0.38f, wpp * 2.0f, 0.0f);
}

const aegis::ui::UiButtonStyle kAccentGreen{{0.13f, 0.38f, 0.20f, 0.95f}, {0.20f, 0.52f, 0.30f, 0.97f},
                                            {0.10f, 0.30f, 0.16f, 1.0f},  {0.85f, 1.0f, 0.88f, 1.0f},
                                            {0.35f, 0.85f, 0.45f, 0.45f}};
const aegis::ui::UiButtonStyle kDanger{{0.42f, 0.16f, 0.14f, 0.95f}, {0.56f, 0.22f, 0.18f, 0.97f},
                                       {0.34f, 0.12f, 0.10f, 1.0f},  {1.0f, 0.88f, 0.82f, 1.0f},
                                       {0.95f, 0.45f, 0.38f, 0.45f}};
const aegis::ui::UiButtonStyle kNeutral{{0.17f, 0.20f, 0.28f, 0.95f}, {0.25f, 0.30f, 0.40f, 0.97f},
                                        {0.13f, 0.24f, 0.32f, 1.0f},  {0.88f, 0.91f, 0.96f, 1.0f},
                                        {0.55f, 0.65f, 0.82f, 0.35f}};

void heading_to_render(const core::f64 heading, const core::f64 com_x, const core::f64 com_y,
                       core::f64& offset_x, core::f64& offset_y) noexcept {
    const core::f64 rot = heading - 1.5707963267948966;
    const core::f64 c = std::cos(rot);
    const core::f64 s = std::sin(rot);
    offset_x = com_x * c - com_y * s;
    offset_y = com_x * s + com_y * c;
}

}  // namespace

void scene_state_init(SceneState& state) noexcept {
    state.vab_rig.snap(0.0, 5.6, 52.0f);
    state.vab_rig.camera().set_zoom_limits(28.0f, 96.0f);
    state.flight_rig.snap(0.0, 4.0, 40.0f);
    state.map_rig.snap(0.0, 3000.0, 0.03f);
    state.map_rig.camera().set_zoom_limits(1.0e-4f, 1.0f);
    state.delete_mode = false;
    state.flight_rig_seeded = false;
    state.map_cache_time = -1.0e9;
    state.map_points.clear();
}

void scene_state_reset_flight(SceneState& state) noexcept {
    state.flight_rig_seeded = false;
    state.map_cache_time = -1.0e9;
    state.map_points.clear();
}

// ---------------------------------------------------------------- VAB

void draw_vab(BatchRenderer& renderer, SceneState& state, const aegis::ui::UiInput& input,
              GameSession& session, const core::f64 wall_clock_s) noexcept {
    Camera2D& camera = state.vab_rig.camera();
    const core::f32 vw = camera.viewport_width();
    const core::f32 vh = camera.viewport_height();

    // Interaction: wheel zoom; cursor -> world for the ghost.
    if (input.wheel != 0.0f) {
        state.vab_rig.zoom_wheel(static_cast<core::f64>(input.wheel));
    }
    state.vab_rig.update(0.25);  // glide zoom only; centre stays fixed
    const auto cursor_world = camera.screen_to_world(input.mouse_x, input.mouse_y);
    session.set_cursor_world(cursor_world[0], cursor_world[1]);

    const VabLayout layout{};
    const bool over_palette = point_in(input, layout.palette);
    const bool over_info = point_in(input, layout.info);
    const bool over_launch = point_in(input, layout.launch);
    const bool over_panel = over_palette || over_info || over_launch;

    // Canvas interactions first (widgets get priority via their own hit tests below).
    if (input.mouse_pressed && !over_panel) {
        if (state.delete_mode) {
            (void)session.remove_at_cursor();
        } else {
            (void)session.place_selected();
        }
    }
    if (input.pressed(aegis::ui::UiKey::X)) {
        (void)session.remove_at_cursor();
    }

    // --- world ---------------------------------------------------------
    draw_fullscreen_bands(renderer, camera, {0.075f, 0.085f, 0.115f}, {0.045f, 0.05f, 0.075f}, 8);
    draw_grid(renderer, camera);
    aegis::render::draw_launch_mount(renderer);

    const aegis::parts::VehicleAssembly& assembly = session.build_assembly();
    aegis::render::PartRenderer part_renderer;
    aegis::render::AssemblyDrawOptions options{};
    options.clock_s = static_cast<core::f32>(wall_clock_s);
    options.throttle = 0.0f;
    const HoverPreview preview = session.hover_preview();
    if (preview.valid && preview.parent_index != parts::kInvalidPart) {
        options.highlight_index = preview.parent_index;  // cyan snap outline on the anchor
    }
    part_renderer.draw_assembly(renderer, assembly, 0.0, 0.0, 1.5707963267948966, options);

    // Ghost of the placement candidate.
    if (const aegis::parts::PartTemplate* tmpl = aegis::parts::find_template(session.selected_template());
        tmpl != nullptr && !over_panel) {
        const core::u8 rot = static_cast<core::u8>(aegis::parts::rot_of(preview.rot));
        const core::f32 ghost_rotation =
            static_cast<core::f32>(rot) * (kPi * 0.5f);
        const core::u32 ghost_flags = static_cast<core::u32>(aegis::renderer2d::kPartFlagGhost);
        part_renderer.draw_single(renderer, *tmpl, 4000, preview.origin_x, preview.origin_y, preview.width,
                                  preview.height, ghost_rotation, options, ghost_flags);
        if (preview.valid && preview.has_target) {
            // Pulsing cyan node marker at the snap target.
            const core::f32 pulse = 0.55f + 0.45f * std::sin(static_cast<core::f32>(wall_clock_s) * 5.0f);
            aegis::renderer2d::SpriteInstance dot{};
            dot.x = preview.node_x;
            dot.y = preview.node_y;
            dot.width = 0.55f;
            dot.height = 0.55f;
            dot.r = 0.25f;
            dot.g = 0.95f;
            dot.b = 1.0f;
            dot.a = 0.35f + 0.55f * pulse;
            dot.softness = -1.0f;
            renderer.draw_sprite(dot);
        } else {
            // Red outline — cannot place here.
            renderer.draw_line(preview.origin_x - preview.width * 0.5f, preview.origin_y - preview.height * 0.5f,
                               preview.origin_x + preview.width * 0.5f, preview.origin_y - preview.height * 0.5f,
                               1.0f, 0.25f, 0.22f, 0.9f, camera.world_per_pixel() * 2.0f, 0.0f);
            renderer.draw_line(preview.origin_x - preview.width * 0.5f, preview.origin_y + preview.height * 0.5f,
                               preview.origin_x + preview.width * 0.5f, preview.origin_y + preview.height * 0.5f,
                               1.0f, 0.25f, 0.22f, 0.9f, camera.world_per_pixel() * 2.0f, 0.0f);
            renderer.draw_line(preview.origin_x - preview.width * 0.5f, preview.origin_y - preview.height * 0.5f,
                               preview.origin_x - preview.width * 0.5f, preview.origin_y + preview.height * 0.5f,
                               1.0f, 0.25f, 0.22f, 0.9f, camera.world_per_pixel() * 2.0f, 0.0f);
            renderer.draw_line(preview.origin_x + preview.width * 0.5f, preview.origin_y - preview.height * 0.5f,
                               preview.origin_x + preview.width * 0.5f, preview.origin_y + preview.height * 0.5f,
                               1.0f, 0.25f, 0.22f, 0.9f, camera.world_per_pixel() * 2.0f, 0.0f);
        }
    }

    // --- UI ------------------------------------------------------------
    aegis::ui::UiContext& ui = state.ui;
    ui.begin_frame(input, vw, vh);

    // Top bar.
    ui.fill_rect(0.0f, 0.0f, vw, 34.0f, aegis::ui::rgb(0.06f, 0.07f, 0.10f, 0.94f));
    ui.text(14.0f, 8.0f, 17.0f, "AEGIS2D  —  VEHICLE ASSEMBLY", aegis::ui::rgb(0.80f, 0.88f, 1.0f));
    ui.text(vw - 14.0f, 9.0f, 13.0f, "GRID 1x1  |  Y-UP  |  1 WU = 1 M", aegis::ui::rgb(0.45f, 0.50f, 0.60f),
            aegis::ui::TextAlign::Right);

    // Palette panel.
    ui.panel(layout.palette.x, layout.palette.y, layout.palette.w, layout.palette.h,
             aegis::ui::rgb(0.085f, 0.095f, 0.135f, 0.92f), aegis::ui::rgb(0.30f, 0.36f, 0.48f, 0.5f));
    ui.text(layout.palette.x + 12.0f, layout.palette.y + 8.0f, 15.0f, "PALETTE", aegis::ui::rgb(0.65f, 0.74f, 0.90f));
    const float button_w = 88.0f, button_h = 34.0f, gap = 8.0f;
    for (core::u32 i = 0; i < 13; ++i) {
        const std::size_t column = i % 2, row = i / 2;
        const float bx = layout.palette.x + 12.0f + static_cast<float>(column) * (button_w + gap);
        const float by = layout.palette.y + 34.0f + static_cast<float>(row) * (button_h + gap);
        const core::u64 id = aegis::ui::UiContext::make_id("palette", i);
        const bool selected = session.selected_template() == static_cast<parts::TemplateId>(i);
        aegis::ui::UiButtonStyle style = kNeutral;
        if (selected) {
            style.idle = {0.16f, 0.34f, 0.46f, 0.97f};
            style.hover = {0.20f, 0.42f, 0.56f, 1.0f};
            style.border = {0.35f, 0.85f, 1.0f, 0.7f};
        }
        if (ui.button(id, bx, by, button_w, button_h, kTemplateShortName[i], style)) {
            session.select_template(static_cast<parts::TemplateId>(i));
        }
    }
    // Delete mode toggle.
    bool delete_mode = state.delete_mode;
    const core::u64 delete_id = aegis::ui::UiContext::make_id("delete_mode");
    if (ui.toggle(delete_id, layout.palette.x + 12.0f, layout.palette.y + layout.palette.h - 40.0f, 34.0f, 20.0f,
                  delete_mode, "DELETE MODE")) {
        state.delete_mode = delete_mode;
    }
    ui.text(layout.palette.x + 12.0f, layout.palette.y + layout.palette.h - 62.0f, 11.0f,
            "X / DELETE: REMOVE PART", aegis::ui::rgb(0.48f, 0.53f, 0.63f));

    // Info panel.
    ui.panel(layout.info.x, layout.info.y, layout.info.w, layout.info.h,
             aegis::ui::rgb(0.085f, 0.095f, 0.135f, 0.92f), aegis::ui::rgb(0.30f, 0.36f, 0.48f, 0.5f));
    ui.text(layout.info.x + 12.0f, layout.info.y + 8.0f, 15.0f, "VEHICLE", aegis::ui::rgb(0.65f, 0.74f, 0.90f));
    char buffer[160];
    std::snprintf(buffer, sizeof(buffer), "CRAFT %s (%u/%u)", session.preset_name(), session.preset_index() + 1,
                  aegis::game::kPresetCount);
    ui.text(layout.info.x + 12.0f, layout.info.y + 24.0f, 11.0f, buffer, aegis::ui::rgb(0.55f, 0.80f, 0.95f));
    std::snprintf(buffer, sizeof(buffer), "MASS   %8.0f KG", session.build_mass_kg());
    ui.text(layout.info.x + 12.0f, layout.info.y + 44.0f, 13.0f, buffer, aegis::ui::rgb(0.85f, 0.88f, 0.93f));
    std::snprintf(buffer, sizeof(buffer), "DELTA-V %6.0f M/S", session.delta_v_estimate());
    ui.text(layout.info.x + 12.0f, layout.info.y + 64.0f, 13.0f, buffer, aegis::ui::rgb(0.85f, 0.88f, 0.93f));
    std::snprintf(buffer, sizeof(buffer), "PARTS  %8u", session.part_count());
    ui.text(layout.info.x + 12.0f, layout.info.y + 84.0f, 13.0f, buffer, aegis::ui::rgb(0.85f, 0.88f, 0.93f));

    ui.text(layout.info.x + 12.0f, layout.info.y + 114.0f, 13.0f, "STAGING", aegis::ui::rgb(0.65f, 0.74f, 0.90f));
    const auto& stages = session.staging().stages();
    float stage_y = layout.info.y + 134.0f;
    for (std::size_t s = 0; s < stages.size() && s < 6; ++s) {
        core::u32 engines = 0, decouplers = 0, fairings = 0;
        for (const auto& item : stages[s].items) {
            if (item.action == aegis::parts::StageAction::IgniteEngine) ++engines;
            else if (item.action == aegis::parts::StageAction::FireDecoupler) ++decouplers;
            else ++fairings;
        }
        std::snprintf(buffer, sizeof(buffer), "STG %zu  IGN x%u  SEP x%u  FAIR x%u", s, engines, decouplers, fairings);
        ui.text(layout.info.x + 12.0f, stage_y, 12.0f, buffer, aegis::ui::rgb(0.75f, 0.82f, 0.90f));
        stage_y += 20.0f;
    }
    if (stages.empty()) {
        ui.text(layout.info.x + 12.0f, stage_y, 12.0f, "NO ASSIGNMENTS", aegis::ui::rgb(0.60f, 0.45f, 0.40f));
    }
    const float buttons_y = layout.info.y + 258.0f;
    const core::u64 auto_id = aegis::ui::UiContext::make_id("auto_stage");
    if (ui.button(auto_id, layout.info.x + 12.0f, buttons_y, 96.0f, 30.0f, "AUTO-STAGE", kNeutral)) {
        session.auto_stage();
    }
    const core::u64 sym_id = aegis::ui::UiContext::make_id("symmetry");
    const char* sym_label = session.symmetry() == Symmetry::MirrorVertical ? "SYM: MIRROR" : "SYM: OFF";
    if (ui.button(sym_id, layout.info.x + 116.0f, buttons_y, 90.0f, 30.0f, sym_label, kNeutral)) {
        session.cycle_symmetry();
    }
    const core::u64 clear_id = aegis::ui::UiContext::make_id("clear");
    if (ui.button(clear_id, layout.info.x + 12.0f, buttons_y + 40.0f, 94.0f, 30.0f, "CLEAR", kDanger)) {
        session.clear_assembly();
    }
    const core::u64 demo_id = aegis::ui::UiContext::make_id("demo");
    if (ui.button(demo_id, layout.info.x + 114.0f, buttons_y + 40.0f, 96.0f, 30.0f, "RELOAD", kNeutral)) {
        session.reset_demo();
    }
    // Preset selection strip (keys 1..3 + clickable).
    const core::u64 preset_id = aegis::ui::UiContext::make_id("preset_strip");
    (void)preset_id;
    for (core::u32 slot = 0; slot < aegis::game::kPresetCount; ++slot) {
        const core::u64 pid = aegis::ui::UiContext::make_id("preset", slot);
        const float px = layout.info.x + 12.0f + static_cast<float>(slot) * 68.0f;
        const float py = buttons_y + 78.0f;
        char label[16];
        std::snprintf(label, sizeof(label), "%u", slot + 1);
        const bool active = session.preset_index() == slot;
        aegis::ui::UiButtonStyle style = kNeutral;
        if (active) {
            style.idle = {0.16f, 0.34f, 0.46f, 0.97f};
            style.hover = {0.20f, 0.42f, 0.56f, 1.0f};
            style.border = {0.35f, 0.85f, 1.0f, 0.7f};
        }
        if (ui.button(pid, px, py, 60.0f, 26.0f, label, style)) {
            session.load_preset(slot);
        }
    }

    // Launch blocker + big LAUNCH button.
    const std::string blocker = session.launch_blocker();
    if (!blocker.empty()) {
        ui.text(layout.launch.x + layout.launch.w * 0.5f, layout.launch.y - 20.0f, 12.0f, blocker.c_str(),
                aegis::ui::rgb(0.95f, 0.55f, 0.45f), aegis::ui::TextAlign::Center);
    }
    const core::u64 launch_id = aegis::ui::UiContext::make_id("launch");
    if (ui.button(launch_id, layout.launch.x, layout.launch.y, layout.launch.w, layout.launch.h, "LAUNCH",
                  blocker.empty() ? kAccentGreen : kNeutral)) {
        (void)session.launch();
    }

    // Controls hint, bottom-left.
    ui.text(16.0f, vh - 26.0f, 11.0f,
            "WHEEL ZOOM   LMB PLACE   X REMOVE   S SYMMETRY   1/2/3 CRAFT   ESC QUIT",
            aegis::ui::rgb(0.42f, 0.47f, 0.56f));

    // Toast line.
    if (wall_clock_s - session.toast_time() < 3.0 && !session.toast().empty()) {
        ui.text(vw * 0.5f, 40.0f, 13.0f, session.toast().c_str(), aegis::ui::rgb(0.95f, 0.85f, 0.45f),
                aegis::ui::TextAlign::Center);
    }

    aegis::render::submit_ui(renderer, camera, ui);
}

// ---------------------------------------------------------------- flight

namespace {

void update_map_cache(SceneState& state, const aegis::physics::VehicleFlight& flight, const core::f64 now) {
    constexpr core::f64 kRefreshPeriod = 0.5;  // seconds of flight time
    if (now - state.map_cache_time < kRefreshPeriod && !state.map_points.empty()) {
        return;
    }
    state.map_cache_time = now;
    state.map_points.clear();
    state.map_apoapsis_alt = -1.0;
    state.map_periapsis_alt = -1.0;
    state.map_period_s = 0.0;

    const aegis::physics::FlightState& fs = flight.state();
    state.map_elements = aegis::orbit::elements_from_state({fs.position.x, fs.position.y},
                                                           {fs.velocity.x, fs.velocity.y}, kEarthMu, fs.time);
    if (state.map_elements.eccentricity >= 1.0) {
        return;  // unbound — no closed polyline; caller prints the caveat
    }
    state.map_points = aegis::orbit::sample_path(state.map_elements, 128);
    for (auto& point : state.map_points) {
        point[1] = static_cast<core::f32>(static_cast<core::f64>(point[1]) - kEarthRadius);  // rebase
    }
    state.map_apoapsis_alt = aegis::orbit::apoapsis_radius(state.map_elements) - kEarthRadius;
    state.map_periapsis_alt = aegis::orbit::periapsis_radius(state.map_elements) - kEarthRadius;
    state.map_period_s = aegis::orbit::orbital_period(state.map_elements);
}

void draw_trajectory(BatchRenderer& renderer, const aegis::renderer2d::Camera2D& camera,
                     const std::vector<std::array<core::f32, 2>>& points) noexcept {
    if (points.size() < 2) {
        return;
    }
    const core::f32 wpp = camera.world_per_pixel();
    const core::f32 thickness = wpp * 2.2f;
    auto above_surface = [&](const std::array<core::f32, 2>& p) {
        const core::f64 x = p[0], y = static_cast<core::f64>(p[1]) + kEarthRadius;
        return x * x + y * y >= kEarthRadius * kEarthRadius;
    };
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        if (!above_surface(points[i]) || !above_surface(points[i + 1])) {
            continue;  // segment dips into the planet — clip it out
        }
        renderer.draw_line(points[i][0], points[i][1], points[i + 1][0], points[i + 1][1], 0.46f, 0.66f, 1.0f,
                           0.6f, thickness, 0.1f);
    }
}

void draw_apsis_marker(BatchRenderer& renderer, const aegis::renderer2d::Camera2D& camera,
                       const core::f64 angle, const core::f64 radius_abs, const bool is_apo) noexcept {
    // Position on the apsidal line; world is planet-centred with y-rebase.
    const core::f32 mx = static_cast<core::f32>(radius_abs * std::cos(angle));
    const core::f32 my = static_cast<core::f32>(radius_abs * std::sin(angle) - kEarthRadius);
    const core::f32 wpp = camera.world_per_pixel();
    const core::f32 size = 10.0f * wpp;
    aegis::renderer2d::SpriteInstance dot{};
    dot.x = mx;
    dot.y = my;
    dot.width = size;
    dot.height = size;
    dot.r = is_apo ? 0.75f : 0.55f;
    dot.g = is_apo ? 0.6f : 0.9f;
    dot.b = is_apo ? 0.95f : 0.55f;
    dot.a = 0.95f;
    dot.softness = -1.0f;
    renderer.draw_sprite(dot);
}

}  // namespace

void draw_flight(BatchRenderer& renderer, SceneState& state, const aegis::ui::UiInput& input,
                 GameSession& session) noexcept {
    const aegis::physics::VehicleFlight* flight = session.flight();
    if (flight == nullptr) {
        return;  // Build screen: draw_vab owns the frame
    }
    const aegis::physics::FlightState& fs = flight->state();
    const core::f64 now = session.flight_time();

    // Seed the rig on the first flight frame (and after every abort/launch cycle).
    if (!state.flight_rig_seeded) {
        state.flight_rig.snap(fs.position.x, fs.position.y - kEarthRadius + 3.0, 40.0f);
        state.flight_rig_seeded = true;
    }

    aegis::render::CameraRig& active_rig = session.map_overlay() ? state.map_rig : state.flight_rig;
    Camera2D& camera = active_rig.camera();
    const core::f32 vw = camera.viewport_width();
    const core::f32 vh = camera.viewport_height();

    // Rebased render coordinates (planet centre sits at y' = -R).
    const core::f64 rocket_x = fs.position.x;
    const core::f64 rocket_y = fs.position.y - kEarthRadius;
    const core::f64 altitude = session.altitude_m();
    const core::f32 altitude_fraction = static_cast<core::f32>(std::clamp(altitude / 100000.0, 0.0, 1.0));

    if (session.map_overlay()) {
        update_map_cache(state, *flight, now);
        // Frame the whole trajectory: target the arc's centroid-ish point.
        const core::f64 apo = std::max(state.map_apoapsis_alt, 2500.0);
        const core::f64 fit_height = apo * 1.45 + 1500.0;
        state.map_rig.set_target(0.0, rocket_y * 0.30);
        state.map_rig.set_zoom(static_cast<core::f32>(static_cast<core::f64>(vh) / fit_height));
        state.map_rig.update(1.0 / 60.0);
    } else {
        // Follow the CoM.
        state.flight_rig.set_target(rocket_x, rocket_y);
        if (input.wheel != 0.0f) {
            state.flight_rig.zoom_wheel(static_cast<core::f64>(input.wheel));
        }
        state.flight_rig.update(1.0 / 60.0);
    }

    aegis::ui::UiContext& ui = state.ui;
    ui.begin_frame(input, vw, vh);

    aegis::render::PartRenderer part_renderer;
    aegis::render::AssemblyDrawOptions options{};
    options.clock_s = static_cast<core::f32>(now);
    options.throttle = static_cast<core::f32>(session.throttle());

    if (session.map_overlay()) {
        // --- full-screen map mode ---------------------------------------
        draw_fullscreen_bands(renderer, camera, {0.004f, 0.006f, 0.016f}, {0.030f, 0.038f, 0.075f}, 1);
        aegis::render::draw_planet(renderer, 0.0, -kEarthRadius, {});
        draw_trajectory(renderer, camera, state.map_points);
        // Apoapsis / periapsis markers.
        const core::f64 omega = state.map_elements.periapsis_angle;
        if (state.map_elements.eccentricity < 1.0) {
            const core::f64 a = state.map_elements.semi_major_axis;
            const core::f64 e = state.map_elements.eccentricity;
            draw_apsis_marker(renderer, camera, omega + 3.14159265358979323846, a * (1.0 + e), true);
            if (a * (1.0 - e) > kEarthRadius) {
                draw_apsis_marker(renderer, camera, omega, a * (1.0 - e), false);
            }
        }
        // Vessel dot.
        aegis::renderer2d::SpriteInstance vessel{};
        const core::f32 wpp = camera.world_per_pixel();
        vessel.x = static_cast<core::f32>(rocket_x);
        vessel.y = static_cast<core::f32>(rocket_y);
        vessel.width = 9.0f * wpp;
        vessel.height = 9.0f * wpp;
        vessel.r = 1.0f;
        vessel.g = 0.72f;
        vessel.b = 0.25f;
        vessel.a = 1.0f;
        vessel.softness = -1.0f;
        renderer.draw_sprite(vessel);

        char buffer[160];
        ui.text(22.0f, 16.0f, 22.0f, "ORBITAL MAP", aegis::ui::rgb(0.75f, 0.87f, 1.0f));
        ui.text(22.0f, 46.0f, 12.0f, "PREDICTED KEPLERIAN TRAJECTORY  (M) RETURN", aegis::ui::rgb(0.45f, 0.52f, 0.65f));
        std::snprintf(buffer, sizeof(buffer), "APOAPSIS   %9.1f M", session.apoapsis_m());
        ui.text(22.0f, 78.0f, 14.0f, buffer, aegis::ui::rgb(0.85f, 0.82f, 0.98f));
        std::snprintf(buffer, sizeof(buffer), "PERIAPSIS  %9.1f M", session.periapsis_m());
        ui.text(22.0f, 100.0f, 14.0f, buffer, aegis::ui::rgb(0.75f, 0.92f, 0.75f));
        std::snprintf(buffer, sizeof(buffer), "ALTITUDE   %9.1f M", altitude);
        ui.text(22.0f, 122.0f, 14.0f, buffer, aegis::ui::rgb(0.85f, 0.88f, 0.93f));
        std::snprintf(buffer, sizeof(buffer), "VELOCITY   %9.1f M/S", session.speed_m_s());
        ui.text(22.0f, 144.0f, 14.0f, buffer, aegis::ui::rgb(0.85f, 0.88f, 0.93f));
        if (state.map_elements.eccentricity >= 1.0) {
            ui.text(22.0f, 172.0f, 13.0f, "UNBOUND TRAJECTORY", aegis::ui::rgb(0.95f, 0.6f, 0.4f));
        } else {
            std::snprintf(buffer, sizeof(buffer), "PERIOD     %9.1f S", state.map_period_s);
            ui.text(22.0f, 172.0f, 14.0f, buffer, aegis::ui::rgb(0.65f, 0.70f, 0.80f));
        }
        aegis::render::submit_ui(renderer, camera, ui);
        return;
    }

    // --- normal flight ---------------------------------------------------
    aegis::render::draw_sky(renderer, camera, altitude_fraction, /*star_seed=*/0xC0FFEEu);
    aegis::render::draw_planet(renderer, 0.0, -kEarthRadius, {});
    aegis::render::draw_ground(renderer, camera);
    aegis::render::draw_launch_mount(renderer);

    // The flying vehicle (render rebase applied).
    const aegis::parts::MassProperties mp = flight->assembly().mass_properties();
    core::f64 com_offset_x = 0.0, com_offset_y = 0.0;
    heading_to_render(fs.heading, static_cast<core::f64>(mp.center_of_mass[0]),
                      static_cast<core::f64>(mp.center_of_mass[1]), com_offset_x, com_offset_y);
    const core::f64 origin_x = rocket_x - com_offset_x;
    const core::f64 origin_y = rocket_y - com_offset_y;
    part_renderer.draw_assembly(renderer, flight->assembly(), origin_x, origin_y, fs.heading, options);
    part_renderer.draw_plumes(renderer, flight->assembly(), origin_x, origin_y, fs.heading,
                              session.throttle(), now, flight->ignited_engines());

    // Detached stages fall ballistically — keep rendering them.
    for (const aegis::physics::DetachedStage& detached : flight->detached_stages()) {
        const aegis::parts::MassProperties dmp = detached.assembly.mass_properties();
        core::f64 dox = 0.0, doy = 0.0;
        heading_to_render(detached.state.heading, static_cast<core::f64>(dmp.center_of_mass[0]),
                          static_cast<core::f64>(dmp.center_of_mass[1]), dox, doy);
        aegis::render::AssemblyDrawOptions det_options{};
        det_options.clock_s = static_cast<core::f32>(now);
        det_options.throttle = 0.0f;
        part_renderer.draw_assembly(renderer, detached.assembly, detached.state.position.x - dox,
                                    detached.state.position.y - kEarthRadius - doy, detached.state.heading,
                                    det_options);
    }

    // --- HUD -------------------------------------------------------------
    char buffer[160];
    // Top-left flight data panel.
    ui.panel(12.0f, 12.0f, 236.0f, 148.0f, aegis::ui::rgb(0.05f, 0.065f, 0.10f, 0.78f),
             aegis::ui::rgb(0.30f, 0.36f, 0.50f, 0.5f));
    std::snprintf(buffer, sizeof(buffer), "T+%6.1f S", now);
    ui.text(24.0f, 20.0f, 13.0f, buffer, aegis::ui::rgb(0.55f, 0.62f, 0.72f));
    std::snprintf(buffer, sizeof(buffer), "ALT  %8.2f KM", altitude * 0.001);
    ui.text(24.0f, 42.0f, 17.0f, buffer, aegis::ui::rgb(0.90f, 0.94f, 1.0f));
    std::snprintf(buffer, sizeof(buffer), "VEL  %8.1f M/S", session.speed_m_s());
    ui.text(24.0f, 66.0f, 17.0f, buffer, aegis::ui::rgb(0.90f, 0.94f, 1.0f));
    std::snprintf(buffer, sizeof(buffer), "APO  %8.2f KM", std::max(session.apoapsis_m(), 0.0) * 0.001);
    ui.text(24.0f, 90.0f, 15.0f, buffer, aegis::ui::rgb(0.78f, 0.80f, 0.98f));
    ui.bar(24.0f, 118.0f, 212.0f, 16.0f, session.propellant_fraction(), aegis::ui::rgb(0.95f, 0.62f, 0.18f),
           aegis::ui::rgb(0.13f, 0.14f, 0.18f), aegis::ui::rgb(0.5f, 0.45f, 0.3f, 0.6f));
    ui.text(24.0f, 136.0f, 10.0f, "PROPELLANT", aegis::ui::rgb(0.55f, 0.52f, 0.42f));

    // Top-right buttons: map toggle + abort.
    const core::u64 map_id = aegis::ui::UiContext::make_id("map_toggle");
    if (ui.button(map_id, vw - 218.0f, 12.0f, 96.0f, 34.0f, "MAP [M]", kNeutral)) {
        session.toggle_map_overlay();
    }
    const core::u64 abort_id = aegis::ui::UiContext::make_id("abort_vab");
    if (ui.button(abort_id, vw - 112.0f, 12.0f, 100.0f, 34.0f, "ABORT", kDanger)) {
        session.abort_to_vab();
        scene_state_reset_flight(state);
        state.flight_rig_seeded = false;
        return;
    }

    // Throttle: vertical slider, right edge, mid-height.
    const core::u64 throttle_id = aegis::ui::UiContext::make_id("throttle");
    const core::f64 throttle = session.throttle();
    const core::f64 new_throttle =
        ui.slider_v(throttle_id, vw - 66.0f, vh * 0.30f, 34.0f, vh * 0.34f, throttle);
    if (new_throttle != throttle) {
        session.set_throttle(new_throttle);
    }
    std::snprintf(buffer, sizeof(buffer), "THR %3.0f%%", throttle * 100.0);
    ui.text(vw - 82.0f, vh * 0.30f + vh * 0.34f + 8.0f, 12.0f, buffer, aegis::ui::rgb(0.85f, 0.88f, 0.93f));

    // Stage button, bottom-right: shows the NEXT stage action.
    const auto& stages = flight->staging().stages();
    const core::u8 cursor = flight->staging().cursor();
    const char* stage_label = cursor < stages.size() ? "STAGE" : "OUT OF STAGES";
    const core::u64 stage_id = aegis::ui::UiContext::make_id("stage_button");
    if (ui.button(stage_id, vw - 236.0f, vh - 96.0f, 150.0f, 60.0f, stage_label,
                  cursor < stages.size() ? kAccentGreen : kNeutral)) {
        session.activate_stage();
    }
    std::snprintf(buffer, sizeof(buffer), "STAGE %u/%zu", cursor, stages.size());
    ui.text(vw - 161.0f, vh - 118.0f, 11.0f, buffer, aegis::ui::rgb(0.55f, 0.62f, 0.72f),
            aegis::ui::TextAlign::Center);

    // Controls hint, bottom-left.
    ui.text(16.0f, vh - 44.0f, 11.0f, "A/D OR Q/E  PITCH     W/S  THROTTLE", aegis::ui::rgb(0.42f, 0.47f, 0.56f));
    ui.text(16.0f, vh - 26.0f, 11.0f, "SPACE  STAGE     M  MAP     ESC  ABORT TO VAB", aegis::ui::rgb(0.42f, 0.47f, 0.56f));

    // Toast (fades after 3 s).
    if (now - session.toast_time() < 3.0 && !session.toast().empty()) {
        ui.text(vw * 0.5f, vh * 0.16f, 15.0f, session.toast().c_str(), aegis::ui::rgb(0.98f, 0.88f, 0.45f),
                aegis::ui::TextAlign::Center);
    }

    // Impact splash.
    if (session.impacted()) {
        ui.text(vw * 0.5f, vh * 0.42f, 34.0f, "IMPACT", aegis::ui::rgb(1.0f, 0.55f, 0.4f),
                aegis::ui::TextAlign::Center);
        ui.text(vw * 0.5f, vh * 0.42f + 40.0f, 13.0f, "ESC RETURNS TO THE VAB", aegis::ui::rgb(0.75f, 0.7f, 0.65f),
                aegis::ui::TextAlign::Center);
    }

    aegis::render::submit_ui(renderer, camera, ui);
}


aegis::renderer2d::Camera2D& scene_active_camera(SceneState& state, const GameSession& session) noexcept {
    if (session.screen() == GameScreen::Build) {
        return state.vab_rig.camera();
    }
    return session.map_overlay() ? state.map_rig.camera() : state.flight_rig.camera();
}

void scene_set_viewport(SceneState& state, const core::f32 width_px, const core::f32 height_px) noexcept {
    state.vab_rig.camera().set_viewport(width_px, height_px);
    state.flight_rig.camera().set_viewport(width_px, height_px);
    state.map_rig.camera().set_viewport(width_px, height_px);
}

}  // namespace aegis::game
