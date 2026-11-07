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
#include <cstdlib>

using aegis::renderer2d::BatchRenderer;
using aegis::renderer2d::Camera2D;

namespace aegis::game {
namespace {

constexpr core::f64 kEarthMu = 3.986004418e14;
constexpr core::f64 kEarthRadius = 6.371e6;       ///< render rebase: world y' = y - R
constexpr core::f32 kPi = 3.14159265358979323846f;

// Palette short labels, indexed by TemplateId (registry order, 0..kTemplateCount-1).
const char* const kTemplateShortName[aegis::parts::kTemplateCount] = {"POD",   "LONG-2", "LONG-4", "SHORT-1", "SPARR", "LNGBW", "KSTRL",
                                                                      "DECOP", "HVY-S",  "RAD-S",  "FAIR",    "NOSE",  "STRUT",
                                                                      "RCS",   "M-TNK",  "LEG"};

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
    const core::f32 thin = wpp * 1.2f;
    // 1-wu lines (barely there), 5-wu majors, and the vertical build axis (x = 0).
    for (core::i32 x = -10; x <= 10; ++x) {
        const core::f32 fx = static_cast<core::f32>(x);
        const bool major = (x % 5) == 0;
        renderer.draw_line(fx, -0.6f, fx, 14.5f, 0.70f, 0.78f, 0.95f, major ? 0.13f : 0.05f, thin, 0.0f);
    }
    for (core::i32 y = 0; y <= 14; ++y) {
        const core::f32 fy = static_cast<core::f32>(y);
        const bool major = (y % 5) == 0;
        renderer.draw_line(-11.0f, fy, 11.0f, fy, 0.70f, 0.78f, 0.95f, major ? 0.12f : 0.045f, thin, 0.0f);
    }
    renderer.draw_line(0.5f, -0.6f, 0.5f, 14.5f, 1.0f, 0.62f, 0.25f, 0.32f, wpp * 2.0f, 0.0f);
}

// Shared engineering-dark tokens (VAB + flight HUD + map).
const aegis::ui::UiColor kPanelFill{0.066f, 0.078f, 0.124f, 0.92f};
const aegis::ui::UiColor kPanelBorder{0.165f, 0.208f, 0.306f, 0.95f};
const aegis::ui::UiColor kAccent{0.353f, 0.851f, 1.0f, 1.0f};
const aegis::ui::UiColor kAccentDim{0.353f, 0.851f, 1.0f, 0.5f};
const aegis::ui::UiColor kTextHi{0.91f, 0.925f, 0.96f, 1.0f};
const aegis::ui::UiColor kTextDim{0.55f, 0.60f, 0.70f, 1.0f};
const aegis::ui::UiColor kTextFaint{0.36f, 0.40f, 0.48f, 1.0f};

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
    // Bake the vector font atlas (CPU, ~milliseconds, deterministic) and hand
    // it to the UI context — text() measures and emits against this atlas.
    state.ui.set_font(state.font);
    state.vab_rig.snap(0.0, 5.6, 52.0f);
    state.vab_rig.camera().set_zoom_limits(28.0f, 96.0f);
    state.flight_rig.snap(0.0, 4.0, 40.0f);
    state.map_rig.snap(0.0, 3000.0, 0.03f);
    state.map_rig.camera().set_zoom_limits(1.0e-4f, 1.0f);
    state.delete_mode = false;
    state.flight_rig_seeded = false;
    state.map_seeded = false;
    state.map_cache_time = -1.0e9;
    state.map_points.clear();
    state.map_ballistic_arc.clear();
    state.map_impact_pending = false;
    state.map_impact_in_s = -1.0;
    state.map_t_ap_s = state.map_t_pe_s = -1.0;
}

void scene_state_reset_flight(SceneState& state) noexcept {
    state.flight_rig_seeded = false;
    state.map_seeded = false;
    state.map_cache_time = -1.0e9;
    state.map_points.clear();
    state.map_ballistic_arc.clear();
    state.map_impact_pending = false;
    state.map_impact_in_s = -1.0;
    state.map_t_ap_s = state.map_t_pe_s = -1.0;
}

// ---------------------------------------------------------------- VAB

void draw_vab(BatchRenderer& renderer, SceneState& state, const aegis::ui::UiInput& input,
              GameSession& session, const core::f64 wall_clock_s) noexcept {
    Camera2D& camera = state.vab_rig.camera();
    const core::f32 vw = camera.viewport_width();
    const core::f32 vh = camera.viewport_height();
    const float kHeaderH = 52.0f;

    // Interaction: wheel zoom; cursor -> world for the ghost.
    if (input.wheel != 0.0f) {
        state.vab_rig.zoom_wheel(static_cast<core::f64>(input.wheel));
    }
    state.vab_rig.update(0.25);  // glide zoom only; centre stays fixed
    const auto cursor_world = camera.screen_to_world(input.mouse_x, input.mouse_y);
    session.set_cursor_world(cursor_world[0], cursor_world[1]);

    // Layout: slim top bar, parts palette (left), vessel panel (right).
    // Everything else is open workspace for the vehicle.
    const aegis::ui::UiRect palette{14.0f, kHeaderH + 12.0f, 236.0f, vh - kHeaderH - 12.0f - 78.0f};
    const core::f32 info_w = 272.0f;
    const aegis::ui::UiRect info{vw - info_w - 14.0f, kHeaderH + 12.0f, info_w, vh - kHeaderH - 12.0f - 14.0f};
    // Header strip hosts LAUNCH and the craft switcher — clicks there must
    // never fall through to canvas placement.
    const bool over_header = input.mouse_y >= 0.0f && input.mouse_y < kHeaderH;
    const bool over_palette = point_in(input, palette);
    const bool over_info = point_in(input, info);
    const bool over_panel = over_palette || over_info || over_header;

    // --- keyboard -------------------------------------------------------
    if (input.pressed(aegis::ui::UiKey::S)) {
        session.cycle_symmetry();
    }
    if (input.pressed(aegis::ui::UiKey::G)) {
        state.grid_visible = !state.grid_visible;
    }

    // Canvas interactions first (widgets get priority via their own hit tests below).
    if (input.mouse_pressed && !over_panel) {
        if (state.delete_mode) {
            (void)session.remove_at_cursor();
        } else if (session.place_selected()) {
            // SFS-style snap flash: expanding ring at the freshly placed node.
            const auto anchor = session.last_placement();
            state.flash_x = anchor[0];
            state.flash_y = anchor[1];
            state.flash_time = wall_clock_s;
        }
    }
    if (input.pressed(aegis::ui::UiKey::X)) {
        (void)session.remove_at_cursor();
    }

    // --- world ---------------------------------------------------------
    draw_fullscreen_bands(renderer, camera, {0.094f, 0.102f, 0.130f}, {0.038f, 0.043f, 0.062f}, 10);
    if (state.grid_visible) {
        draw_grid(renderer, camera);
    }
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

    // Placement snap flash — expanding ring, ~0.45 s.
    if (wall_clock_s - state.flash_time < 0.45) {
        const core::f32 t = static_cast<core::f32>((wall_clock_s - state.flash_time) / 0.45);
        aegis::renderer2d::SpriteInstance ring{};
        ring.x = state.flash_x;
        ring.y = state.flash_y;
        ring.width = 0.35f + 1.35f * t;
        ring.height = ring.width;
        ring.r = 0.55f;
        ring.g = 1.0f;
        ring.b = 0.85f;
        ring.a = (1.0f - t) * 0.95f;
        ring.softness = -1.0f;
        renderer.draw_sprite(ring);
    }

    const bool ghost_visible = aegis::parts::find_template(session.selected_template()) != nullptr && !over_panel;

    // Ghost of the placement candidate + live snap-point markers.
    if (ghost_visible) {
        const aegis::parts::PartTemplate* tmpl = aegis::parts::find_template(session.selected_template());
        const core::f32 wpp = camera.world_per_pixel();
        // Every currently placeable node renders as a dim cyan ring; the chosen
        // one gets the bright pulse below.
        for (core::u32 i = 0; i < preview.node_marker_count; ++i) {
            const CompatibleNodeMarker& marker = preview.node_markers[i];
            aegis::renderer2d::SpriteInstance dot{};
            dot.x = marker.x;
            dot.y = marker.y;
            dot.width = 0.5f;
            dot.height = 0.5f;
            dot.r = 0.30f;
            dot.g = 0.92f;
            dot.b = 1.0f;
            dot.a = marker.chosen ? 0.0f : 0.45f;
            dot.softness = -1.0f;  // ring
            renderer.draw_sprite(dot);
        }
        // Snap line from the ghost socket to the chosen node.
        if (preview.has_target) {
            renderer.draw_line(preview.origin_x, preview.origin_y, preview.node_x, preview.node_y,
                               0.30f, 0.92f, 1.0f, 0.34f, wpp * 1.6f, 0.0f);
        }
        const core::u8 rot = static_cast<core::u8>(aegis::parts::rot_of(preview.rot));
        const core::f32 ghost_rotation = static_cast<core::f32>(rot) * (kPi * 0.5f);
        const core::u32 ghost_flags = static_cast<core::u32>(aegis::renderer2d::kPartFlagGhost);
        part_renderer.draw_single(renderer, *tmpl, 4000, preview.origin_x, preview.origin_y, preview.width,
                                  preview.height, ghost_rotation, options, ghost_flags);
        if (preview.valid && preview.has_target) {
            // Pulsing cyan node marker at the snap target.
            const core::f32 pulse = 0.55f + 0.45f * std::sin(static_cast<core::f32>(wall_clock_s) * 5.0f);
            aegis::renderer2d::SpriteInstance dot{};
            dot.x = preview.node_x;
            dot.y = preview.node_y;
            dot.width = 0.62f;
            dot.height = 0.62f;
            dot.r = 0.30f;
            dot.g = 0.95f;
            dot.b = 1.0f;
            dot.a = 0.40f + 0.60f * pulse;
            dot.softness = -1.0f;
            renderer.draw_sprite(dot);
        } else if (!preview.valid) {
            // Red corner brackets — cannot place here.
            const core::f32 hx = preview.width * 0.5f;
            const core::f32 hy = preview.height * 0.5f;
            const core::f32 l = 0.45f;  // bracket length (wu)
            const core::f32 t = wpp * 2.0f;
            const std::array<std::pair<core::f32, core::f32>, 4> corners{{{-hx, -hy}, {hx, -hy}, {-hx, hy}, {hx, hy}}};
            for (const auto& [sx, sy] : corners) {
                const core::f32 sgn_x = sx < 0.0f ? 1.0f : -1.0f;
                const core::f32 sgn_y = sy < 0.0f ? 1.0f : -1.0f;
                renderer.draw_line(preview.origin_x + sx, preview.origin_y + sy,
                                   preview.origin_x + sx + sgn_x * l, preview.origin_y + sy,
                                   1.0f, 0.30f, 0.28f, 0.95f, t, 0.0f);
                renderer.draw_line(preview.origin_x + sx, preview.origin_y + sy,
                                   preview.origin_x + sx, preview.origin_y + sy + sgn_y * l,
                                   1.0f, 0.30f, 0.28f, 0.95f, t, 0.0f);
            }
        }
        // Delete-mode: red ring at the cursor.
        // Pad-root placement (empty vehicle): bright base line on the deck.
        if (preview.valid && preview.parent_index == parts::kInvalidPart) {
            renderer.draw_line(preview.origin_x - preview.width * 0.5f, 0.02f,
                               preview.origin_x + preview.width * 0.5f, 0.02f,
                               0.35f, 0.95f, 1.0f, 0.9f, wpp * 2.4f, 0.0f);
        }
        // Mirror ghost: show the twin that symmetry will place.
        if (preview.valid && session.symmetry() == Symmetry::MirrorVertical &&
            preview.parent_index != parts::kInvalidPart) {
            core::f32 axis_wu = 0.5f;
            for (const parts::Part& part : assembly.parts()) {
                if (part.tmpl == parts::kInvalidTemplate) {
                    continue;
                }
                const parts::PartTemplate* ptmpl = aegis::parts::find_template(part.tmpl);
                if (ptmpl != nullptr && ptmpl->category == parts::PartCategory::CommandPod) {
                    axis_wu = assembly.part_center_wu(static_cast<core::u32>(&part - &assembly.parts()[0]))[0];
                    break;
                }
            }
            const core::f32 mirrored_x = 2.0f * axis_wu - preview.origin_x;
            const core::u8 mirrored_rot = static_cast<core::u8>(preview.rot ^ aegis::parts::kFlipBit);
            const core::f32 mirrored_rotation =
                static_cast<core::f32>(aegis::parts::rot_of(mirrored_rot)) * (kPi * 0.5f);
            core::u32 mirror_flags = static_cast<core::u32>(aegis::renderer2d::kPartFlagGhost);
            if (aegis::parts::flipped(mirrored_rot)) {
                mirror_flags |= static_cast<core::u32>(aegis::renderer2d::kPartFlagFlip);
            }
            part_renderer.draw_single(renderer, *tmpl, 4001, mirrored_x, preview.origin_y, preview.width,
                                      preview.height, mirrored_rotation, options, mirror_flags);
        }
        if (state.delete_mode) {
            aegis::renderer2d::SpriteInstance ring{};
            ring.x = static_cast<core::f32>(cursor_world[0]);
            ring.y = static_cast<core::f32>(cursor_world[1]);
            ring.width = 0.8f;
            ring.height = 0.8f;
            ring.r = 1.0f;
            ring.g = 0.38f;
            ring.b = 0.32f;
            ring.a = 0.85f;
            ring.softness = -1.0f;
            renderer.draw_sprite(ring);
        }
    }

    // --- UI ------------------------------------------------------------
    aegis::ui::UiContext& ui = state.ui;
    ui.begin_frame(input, vw, vh);

    // Design tokens — SFS-inspired slate, high contrast, one accent.
    const aegis::ui::UiColor c_panel{0.070f, 0.078f, 0.110f, 0.94f};
    const aegis::ui::UiColor c_panel_hi{0.118f, 0.133f, 0.192f, 0.96f};
    const aegis::ui::UiColor c_cell{0.078f, 0.086f, 0.125f, 0.92f};
    const aegis::ui::UiColor c_cell_hi{0.125f, 0.145f, 0.208f, 0.96f};
    const aegis::ui::UiColor c_border{0.165f, 0.192f, 0.286f, 0.95f};
    const aegis::ui::UiColor c_accent{0.353f, 0.851f, 1.0f, 1.0f};
    const aegis::ui::UiColor c_accent_dim{0.353f, 0.851f, 1.0f, 0.5f};
    const aegis::ui::UiColor c_accent_fill{0.063f, 0.192f, 0.243f, 0.96f};
    const aegis::ui::UiColor c_text{0.945f, 0.957f, 0.984f, 1.0f};
    const aegis::ui::UiColor c_dim{0.651f, 0.694f, 0.784f, 1.0f};
    const aegis::ui::UiColor c_faint{0.435f, 0.475f, 0.565f, 1.0f};
    const aegis::ui::UiColor c_green{0.24f, 0.84f, 0.55f, 1.0f};
    const aegis::ui::UiColor c_amber{0.96f, 0.70f, 0.26f, 1.0f};
    const aegis::ui::UiColor c_red{0.95f, 0.43f, 0.36f, 1.0f};

    char buffer[192];
    const double mass_kg = session.build_mass_kg();
    const double delta_v = session.delta_v_estimate();
    const double twr = session.twr_estimate();
    const aegis::ui::UiColor dv_color = delta_v >= 2400.0 ? c_green : (delta_v >= 1400.0 ? c_amber : c_red);
    const aegis::ui::UiColor twr_color = twr >= 1.4 ? c_green : (twr >= 1.0 ? c_amber : c_red);

    // ================= HEADER BAR (52 px) ================================
    ui.fill_rect(0.0f, 0.0f, vw, kHeaderH, aegis::ui::rgb(0.052f, 0.059f, 0.086f, 0.98f));
    ui.fill_rect(0.0f, kHeaderH - 1.0f, vw, 1.0f, c_border);
    // Left: logo chip + wordmark.
    ui.rect_outline(16.0f, 13.0f, 26.0f, 26.0f, c_accent, 1.0f);
    ui.text(29.0f, 17.0f, 15.0f, "A2", c_accent, aegis::ui::TextAlign::Center, aegis::ui::FontWeight::SemiBold);
    ui.text(50.0f, 15.0f, 17.0f, "AEGIS2D", c_text, aegis::ui::TextAlign::Left, aegis::ui::FontWeight::SemiBold);
    ui.text(50.0f + ui.text_width_px("AEGIS2D  ", 17.0f, aegis::ui::FontWeight::SemiBold), 18.0f, 12.0f,
            "/  VEHICLE ASSEMBLY", c_dim);
    // Center: craft pill.
    {
        const char* preset = session.preset_name();
        char craft_line[72];
        std::snprintf(craft_line, sizeof(craft_line), "%.28s", preset);
        char num_buf[16];
        std::snprintf(num_buf, sizeof(num_buf), "  %u/%u", session.preset_index() + 1, aegis::game::kPresetCount);
        const float name_w = ui.text_width_px(craft_line, 13.0f, aegis::ui::FontWeight::SemiBold) +
                             ui.text_width_px(num_buf, 10.0f) + 34.0f;
        const float px = vw * 0.5f - name_w * 0.5f;
        ui.fill_rect(px, 11.0f, name_w, 30.0f, aegis::ui::rgb(0.070f, 0.078f, 0.115f, 0.95f));
        ui.rect_outline(px, 11.0f, name_w, 30.0f, c_border, 1.0f);
        ui.text(px + 15.0f, 17.0f, 13.0f, craft_line, c_text, aegis::ui::TextAlign::Left,
                aegis::ui::FontWeight::SemiBold);
        ui.text(px + 15.0f + ui.text_width_px(craft_line, 13.0f, aegis::ui::FontWeight::SemiBold), 18.5f,
                10.0f, num_buf, c_faint);
    }
    // Right: LAUNCH (SFS convention: top-right, unmissable).
    const std::string blocker = session.launch_blocker();
    const bool ready = blocker.empty();
    {
        const core::u64 launch_id = aegis::ui::UiContext::make_id("launch");
        const float lw = 198.0f, lh = 38.0f, lx = vw - 14.0f - lw, ly = 7.0f;
        const bool hovered = ui.mouse_inside(lx, ly, lw, lh);
        if (ready) {
            ui.fill_rect(lx, ly, lw, lh, hovered ? aegis::ui::rgb(0.10f, 0.57f, 0.30f, 1.0f)
                                                 : aegis::ui::rgb(0.085f, 0.49f, 0.26f, 1.0f));
            ui.fill_rect(lx + 1.0f, ly + 1.0f, lw - 2.0f, 2.0f, aegis::ui::rgb(0.36f, 0.95f, 0.55f, 0.9f));
            ui.rect_outline(lx, ly, lw, lh, aegis::ui::rgb(0.16f, 0.72f, 0.35f, 1.0f), 1.0f);
            ui.text(lx + lw * 0.5f, ly + 9.0f, 20.0f, "LAUNCH", aegis::ui::rgb(0.94f, 1.0f, 0.96f, 1.0f),
                    aegis::ui::TextAlign::Center, aegis::ui::FontWeight::SemiBold);
        } else {
            ui.fill_rect(lx, ly, lw, lh, hovered ? aegis::ui::rgb(0.13f, 0.14f, 0.19f, 1.0f)
                                                 : aegis::ui::rgb(0.095f, 0.103f, 0.142f, 1.0f));
            ui.rect_outline(lx, ly, lw, lh, aegis::ui::rgb(0.36f, 0.25f, 0.21f, 1.0f), 1.0f);
            ui.text(lx + lw * 0.5f, ly + 5.0f, 16.0f, "LAUNCH", aegis::ui::rgb(0.42f, 0.45f, 0.53f, 1.0f),
                    aegis::ui::TextAlign::Center, aegis::ui::FontWeight::SemiBold);
            ui.text(lx + lw * 0.5f, ly + 24.5f, 9.0f, "LOCKED — SEE CHECKS", aegis::ui::rgb(0.82f, 0.55f, 0.30f, 1.0f),
                    aegis::ui::TextAlign::Center, aegis::ui::FontWeight::SemiBold);
        }
        if (ui.press_button(launch_id, lx, ly, lw, lh)) {
            (void)session.launch();  // blocked: surfaces the reason as a toast
        }
    }

    // ================= PARTS PALETTE (left) ==============================
    ui.panel_frame(palette.x, palette.y, palette.w, palette.h, c_panel, c_border, c_accent_dim);
    ui.text(palette.x + 16.0f, palette.y + 12.0f, 16.0f, "PARTS", c_text, aegis::ui::TextAlign::Left,
            aegis::ui::FontWeight::SemiBold);
    std::snprintf(buffer, sizeof(buffer), "%zu", aegis::parts::kTemplateCount);
    ui.fill_rect(palette.x + palette.w - 44.0f, palette.y + 11.0f, 28.0f, 19.0f, aegis::ui::rgb(0.10f, 0.11f, 0.16f, 1.0f));
    ui.text(palette.x + palette.w - 30.0f, palette.y + 13.5f, 11.0f, buffer, c_dim, aegis::ui::TextAlign::Center,
            aegis::ui::FontWeight::SemiBold);

    // Category tabs (SFS-style filter row(s) under the title).
    struct PaletteTab {
        const char* label;
        int filter;  // -1 = ALL, 0..6 = PartCategory, 7 = UTILITY (Strut + RcsBlock)
    };
    constexpr PaletteTab kTabs[] = {
        {"ALL", -1},
        {"PODS", static_cast<int>(parts::PartCategory::CommandPod)},
        {"ENGINES", static_cast<int>(parts::PartCategory::Engine)},
        {"TANKS", static_cast<int>(parts::PartCategory::FuelTank)},
        {"SEP", static_cast<int>(parts::PartCategory::Decoupler)},
        {"AERO", static_cast<int>(parts::PartCategory::Fairing)},
        {"UTILITY", 7},
    };
    const auto tab_matches = [&](const parts::PartCategory cat, const int filter) {
        if (filter == -1) {
            return true;
        }
        if (filter == 7) {
            return cat == parts::PartCategory::Strut || cat == parts::PartCategory::RcsBlock;
        }
        return static_cast<int>(cat) == filter;
    };
    {
        float tab_x = palette.x + 12.0f;
        float tab_y = palette.y + 40.0f;
        const float tab_max_x = palette.x + palette.w - 12.0f;
        for (std::size_t ti = 0; ti < std::size(kTabs); ++ti) {
            const float tw = ui.text_width_px(kTabs[ti].label, 10.0f, aegis::ui::FontWeight::SemiBold) + 18.0f;
            if (tab_x + tw > tab_max_x) {
                tab_x = palette.x + 12.0f;
                tab_y += 27.0f;
            }
            const bool active = state.palette_filter == kTabs[ti].filter;
            const bool hovered = ui.mouse_inside(tab_x, tab_y, tw, 21.0f);
            ui.fill_rect(tab_x, tab_y, tw, 21.0f, active ? c_accent_fill : (hovered ? c_cell_hi : c_cell));
            ui.rect_outline(tab_x, tab_y, tw, 21.0f, active ? c_accent : c_border, 1.0f);
            ui.text(tab_x + tw * 0.5f, tab_y + 5.0f, 10.0f, kTabs[ti].label, active ? c_accent : c_dim,
                    aegis::ui::TextAlign::Center, aegis::ui::FontWeight::SemiBold);
            const core::u64 tab_id = aegis::ui::UiContext::make_id("palette_tab", static_cast<core::u32>(ti));
            if (ui.press_button(tab_id, tab_x, tab_y, tw, 21.0f)) {
                state.palette_filter = kTabs[ti].filter;
            }
            tab_x += tw + 6.0f;
        }
    }

    // Visible cells after the category filter.
    int visible[aegis::parts::kTemplateCount];
    core::u32 visible_count = 0;
    for (core::u32 i = 0; i < aegis::parts::kTemplateCount; ++i) {
        const parts::PartTemplate* tmpl = aegis::parts::find_template(static_cast<parts::TemplateId>(i));
        if (tmpl != nullptr && tab_matches(tmpl->category, state.palette_filter)) {
            visible[visible_count++] = static_cast<int>(i);
        }
    }

    const float cell_w = 103.0f, cell_h = 50.0f, cell_gap = 6.0f;
    const float cell_x0 = palette.x + 12.0f, cell_y0 = palette.y + 100.0f;
    for (core::u32 vi = 0; vi < visible_count; ++vi) {
        const int i = visible[vi];
        const std::size_t column = vi % 2, row = vi / 2;
        const float bx = cell_x0 + static_cast<float>(column) * (cell_w + cell_gap);
        const float by = cell_y0 + static_cast<float>(row) * (cell_h + cell_gap);
        if (by + cell_h > palette.y + palette.h - 6.0f) {
            break;  // grid does not scroll (filter to narrow it) — never clip a cell
        }
        const core::u64 id = aegis::ui::UiContext::make_id("palette", static_cast<core::u32>(i));
        const bool selected = session.selected_template() == static_cast<parts::TemplateId>(i);
        const bool hovered = ui.mouse_inside(bx, by, cell_w, cell_h);
        if (ui.press_button(id, bx, by, cell_w, cell_h)) {
            session.select_template(static_cast<parts::TemplateId>(i));
        }
        ui.fill_rect(bx, by, cell_w, cell_h, selected ? c_accent_fill : (hovered ? c_cell_hi : c_cell));
        ui.rect_outline(bx, by, cell_w, cell_h, selected ? c_accent : c_border, selected ? 1.5f : 1.0f);
        ui.text(bx + cell_w * 0.5f, by + cell_h - 10.0f, 10.5f, kTemplateShortName[i],
                selected ? c_text : c_dim, aegis::ui::TextAlign::Center, aegis::ui::FontWeight::SemiBold);
    }

    // ================= TOGGLE CLUSTER (bottom-left) ======================
    {
        const float ty = vh - 68.0f;
        const auto toggle_button = [&](float x, core::u64 id, const char* label, const char* key, bool on,
                                       aegis::ui::UiColor on_color) {
            const float bw = 76.0f, bh = 56.0f;
            const bool hovered = ui.mouse_inside(x, ty, bw, bh);
            ui.fill_rect(x, ty, bw, bh, on ? c_accent_fill : (hovered ? c_panel_hi : aegis::ui::rgb(0.082f, 0.090f, 0.137f, 0.96f)));
            ui.rect_outline(x, ty, bw, bh, on ? on_color : c_border, 1.0f);
            ui.text(x + bw - 7.0f, ty + 5.0f, 9.5f, key, on ? on_color : c_faint, aegis::ui::TextAlign::Right);
            ui.text(x + bw * 0.5f, ty + bh - 16.0f, 10.0f, label, on ? on_color : c_dim,
                    aegis::ui::TextAlign::Center, aegis::ui::FontWeight::SemiBold);
            return ui.press_button(id, x, ty, bw, bh);
        };
        const core::u64 sym_id = aegis::ui::UiContext::make_id("symmetry_row");
        const bool mirror = session.symmetry() == Symmetry::MirrorVertical;
        const float sym_x = 14.0f;
        if (toggle_button(sym_x, sym_id, "MIRROR", "S", mirror, c_accent)) {
            session.cycle_symmetry();
        }
        // Mirror glyph: two bars + centre axis.
        {
            const float gy = ty + 10.0f, gx = sym_x + 38.0f;
            ui.fill_rect(gx - 1.0f, gy - 2.0f, 2.0f, 22.0f, mirror ? c_accent : c_dim);
            ui.fill_rect(gx - 14.0f, gy + 1.0f, 7.0f, 15.0f, mirror ? c_accent : c_dim);
            ui.fill_rect(gx + 7.0f, gy + 1.0f, 7.0f, 15.0f, mirror ? c_accent : c_dim);
        }
        const core::u64 del_id = aegis::ui::UiContext::make_id("delete_row");
        const float del_x = 14.0f + 88.0f;
        if (toggle_button(del_x, del_id, "DELETE", "X", state.delete_mode, c_amber)) {
            state.delete_mode = !state.delete_mode;
        }
        // Trash glyph.
        {
            const float gy = ty + 9.0f, gx = del_x + 38.0f;
            const aegis::ui::UiColor g = state.delete_mode ? c_amber : c_dim;
            ui.rect_outline(gx - 9.0f, gy + 5.0f, 18.0f, 14.0f, g, 1.0f);
            ui.fill_rect(gx - 11.0f, gy + 2.0f, 22.0f, 2.0f, g);
            ui.fill_rect(gx - 4.0f, gy - 1.0f, 8.0f, 2.0f, g);
            ui.fill_rect(gx - 4.0f, gy + 8.0f, 2.0f, 8.0f, g);
            ui.fill_rect(gx + 2.0f, gy + 8.0f, 2.0f, 8.0f, g);
        }
        const core::u64 grid_id = aegis::ui::UiContext::make_id("grid_row");
        const float grid_x = 14.0f + 176.0f;
        if (toggle_button(grid_x, grid_id, "GRID", "G", state.grid_visible, c_accent)) {
            state.grid_visible = !state.grid_visible;
        }
        // Grid glyph.
        {
            const float gy = ty + 8.0f, gx = grid_x + 38.0f;
            const aegis::ui::UiColor g = state.grid_visible ? c_accent : c_dim;
            ui.rect_outline(gx - 10.0f, gy, 20.0f, 20.0f, g, 1.0f);
            ui.fill_rect(gx - 3.5f, gy + 1.0f, 1.5f, 18.0f, g);
            ui.fill_rect(gx + 2.0f, gy + 1.0f, 1.5f, 18.0f, g);
            ui.fill_rect(gx - 9.0f, gy + 6.0f, 18.0f, 1.5f, g);
            ui.fill_rect(gx - 9.0f, gy + 12.5f, 18.0f, 1.5f, g);
        }
    }

    // ================= VESSEL PANEL (right) ==============================
    ui.panel_frame(info.x, info.y, info.w, info.h, c_panel, c_border, c_accent_dim);
    ui.text(info.x + 16.0f, info.y + 12.0f, 16.0f, "VESSEL", c_text, aegis::ui::TextAlign::Left,
            aegis::ui::FontWeight::SemiBold);
    ui.divider(info.x + 16.0f, info.y + 38.0f, info.w - 32.0f, c_border);

    // Big technical readouts — label above value, SFS/KSP style.
    const auto stat_block = [&](float y, const char* label, const char* value, aegis::ui::UiColor value_color) {
        ui.text(info.x + 16.0f, y, 10.0f, label, c_dim, aegis::ui::TextAlign::Left,
                aegis::ui::FontWeight::SemiBold);
        ui.text(info.x + 16.0f, y + 13.0f, 20.0f, value, value_color, aegis::ui::TextAlign::Left,
                aegis::ui::FontWeight::SemiBold);
    };
    std::snprintf(buffer, sizeof(buffer), "%.0f KG", mass_kg);
    stat_block(info.y + 46.0f, "MASS", buffer, c_text);
    std::snprintf(buffer, sizeof(buffer), "%.0f M/S", delta_v);
    stat_block(info.y + 96.0f, "DELTA-V", buffer, dv_color);
    std::snprintf(buffer, sizeof(buffer), "%.2f", twr);
    stat_block(info.y + 146.0f, "TWR  ·  LIFTOFF", buffer, twr_color);
    std::snprintf(buffer, sizeof(buffer), "%u", session.part_count());
    stat_block(info.y + 196.0f, "PARTS", buffer, c_text);

    ui.divider(info.x + 16.0f, info.y + 246.0f, info.w - 32.0f, c_border);

    // Staging — compact, with auto-assign.
    ui.text(info.x + 16.0f, info.y + 254.0f, 13.0f, "STAGING", c_text, aegis::ui::TextAlign::Left,
            aegis::ui::FontWeight::SemiBold);
    {
        const core::u64 auto_id = aegis::ui::UiContext::make_id("auto_stage");
        const float bx = info.x + info.w - 72.0f, by = info.y + 250.0f, bw = 56.0f, bh = 21.0f;
        const bool hovered = ui.mouse_inside(bx, by, bw, bh);
        ui.fill_rect(bx, by, bw, bh, hovered ? c_accent_fill : aegis::ui::rgb(0.06f, 0.09f, 0.13f, 1.0f));
        ui.rect_outline(bx, by, bw, bh, c_accent, 1.0f);
        ui.text(bx + bw * 0.5f, by + 4.5f, 10.0f, "AUTO", c_accent, aegis::ui::TextAlign::Center,
                aegis::ui::FontWeight::SemiBold);
        if (ui.press_button(auto_id, bx, by, bw, bh)) {
            session.auto_stage();
        }
    }
    const auto& stages = session.staging().stages();
    float stage_y = info.y + 280.0f;
    core::u32 total_staged_engines = 0;
    for (std::size_t s = 0; s < stages.size(); ++s) {
        for (const auto& item : stages[s].items) {
            if (item.action == aegis::parts::StageAction::IgniteEngine) {
                ++total_staged_engines;
            }
        }
    }
    for (std::size_t s = 0; s < stages.size() && s < 4; ++s) {
        core::u32 engines = 0, decouplers = 0, fairings = 0;
        for (const auto& item : stages[s].items) {
            if (item.action == aegis::parts::StageAction::IgniteEngine) {
                ++engines;
            } else if (item.action == aegis::parts::StageAction::FireDecoupler) {
                ++decouplers;
            } else {
                ++fairings;
            }
        }
        std::snprintf(buffer, sizeof(buffer), "STG %zu", s + 1);
        ui.text(info.x + 16.0f, stage_y, 12.0f, buffer, c_text, aegis::ui::TextAlign::Left,
                aegis::ui::FontWeight::SemiBold);
        buffer[0] = '\0';
        std::size_t used = 0;
        const auto chip = [&](const char* fmt, unsigned count) {
            if (count == 0) {
                return;
            }
            char part[32];
            std::snprintf(part, sizeof(part), fmt, count);
            // snprintf returns the would-be length; keep `used` honest so the
            // size argument can never underflow.
            used += static_cast<std::size_t>(std::snprintf(buffer + used, sizeof(buffer) - used, "%s%s",
                                                           used ? "   " : "", part));
            used = std::min(used, sizeof(buffer) - 1);
        };
        chip("%u×ENG", engines);
        chip("%u×SEP", decouplers);
        chip("%u×FRG", fairings);
        ui.text(info.x + info.w - 16.0f, stage_y + 0.5f, 11.0f, buffer, c_dim, aegis::ui::TextAlign::Right,
                aegis::ui::FontWeight::SemiBold);
        stage_y += 25.0f;
    }
    if (stages.size() > 4) {
        std::snprintf(buffer, sizeof(buffer), "+%zu MORE", stages.size() - 4);
        ui.text(info.x + info.w - 16.0f, stage_y, 10.0f, buffer, c_faint, aegis::ui::TextAlign::Right);
        stage_y += 22.0f;
    }
    if (stages.empty()) {
        ui.text(info.x + 16.0f, stage_y, 11.0f, "NO STAGES — PRESS AUTO", c_amber,
                aegis::ui::TextAlign::Left, aegis::ui::FontWeight::SemiBold);
        stage_y += 24.0f;
    }

    // Readiness strip — one loud line instead of a five-row checklist.
    ui.divider(info.x + 16.0f, stage_y + 8.0f, info.w - 32.0f, c_border);
    {
        const float ry = stage_y + 16.0f;
        const aegis::ui::UiColor chip = ready ? c_green : c_red;
        ui.fill_rect(info.x + 16.0f, ry + 2.0f, 10.0f, 10.0f, chip);
        if (ready) {
            ui.text(info.x + 34.0f, ry, 12.0f, "ALL CHECKS PASS", c_green, aegis::ui::TextAlign::Left,
                    aegis::ui::FontWeight::SemiBold);
        } else {
            char blocker_buf[96];
            std::snprintf(blocker_buf, sizeof(blocker_buf), "%.34s", blocker.c_str());
            ui.text(info.x + 34.0f, ry, 11.0f, blocker_buf, c_red, aegis::ui::TextAlign::Left,
                    aegis::ui::FontWeight::SemiBold);
            if (blocker.size() > 34) {
                std::snprintf(blocker_buf, sizeof(blocker_buf), "%.34s", blocker.c_str() + 34);
                ui.text(info.x + 34.0f, ry + 15.0f, 11.0f, blocker_buf, c_red, aegis::ui::TextAlign::Left,
                        aegis::ui::FontWeight::SemiBold);
            }
        }
    }

    // CRAFT presets — pinned to the bottom of the panel.
    const float craft_y = info.y + info.h - 92.0f;
    ui.divider(info.x + 16.0f, craft_y - 10.0f, info.w - 32.0f, c_border);
    ui.text(info.x + 16.0f, craft_y, 10.0f, "CRAFT PRESETS", c_faint, aegis::ui::TextAlign::Left,
            aegis::ui::FontWeight::SemiBold);
    for (core::u32 slot = 0; slot < aegis::game::kPresetCount; ++slot) {
        const core::u64 pid = aegis::ui::UiContext::make_id("preset", slot);
        const float px = info.x + 16.0f + static_cast<float>(slot) * 42.0f;
        const float py = craft_y + 16.0f;
        char label[8];
        std::snprintf(label, sizeof(label), "%u", slot + 1);
        const bool active = session.preset_index() == slot;
        const bool hovered = ui.mouse_inside(px, py, 36.0f, 24.0f);
        ui.fill_rect(px, py, 36.0f, 24.0f, active ? c_accent_fill : (hovered ? c_panel_hi : c_cell));
        ui.rect_outline(px, py, 36.0f, 24.0f, active ? c_accent : c_border, active ? 1.5f : 1.0f);
        ui.text(px + 18.0f, py + 5.5f, 12.0f, label, active ? c_accent : c_dim, aegis::ui::TextAlign::Center,
                aegis::ui::FontWeight::SemiBold);
        if (ui.press_button(pid, px, py, 36.0f, 24.0f)) {
            session.load_preset(slot);
        }
    }
    {
        const core::u64 clear_id = aegis::ui::UiContext::make_id("clear");
        const core::u64 demo_id = aegis::ui::UiContext::make_id("demo");
        const float by = craft_y + 48.0f;
        const float bw = (info.w - 32.0f - 8.0f) * 0.5f;
        {
            const bool hovered = ui.mouse_inside(info.x + 16.0f, by, bw, 24.0f);
            ui.fill_rect(info.x + 16.0f, by, bw, 24.0f, hovered ? aegis::ui::rgb(0.14f, 0.10f, 0.10f, 1.0f)
                                                                : aegis::ui::rgb(0.10f, 0.075f, 0.075f, 0.95f));
            ui.rect_outline(info.x + 16.0f, by, bw, 24.0f, aegis::ui::rgb(0.45f, 0.22f, 0.18f, 1.0f), 1.0f);
            ui.text(info.x + 16.0f + bw * 0.5f, by + 6.0f, 11.0f, "CLEAR", c_red, aegis::ui::TextAlign::Center,
                    aegis::ui::FontWeight::SemiBold);
            if (ui.press_button(clear_id, info.x + 16.0f, by, bw, 24.0f)) {
                session.clear_assembly();
            }
        }
        {
            const float rx = info.x + 16.0f + bw + 8.0f;
            const bool hovered = ui.mouse_inside(rx, by, bw, 24.0f);
            ui.fill_rect(rx, by, bw, 24.0f, hovered ? c_cell_hi : c_cell);
            ui.rect_outline(rx, by, bw, 24.0f, c_border, 1.0f);
            ui.text(rx + bw * 0.5f, by + 6.0f, 11.0f, "RESET DEMO", c_dim, aegis::ui::TextAlign::Center,
                    aegis::ui::FontWeight::SemiBold);
            if (ui.press_button(demo_id, rx, by, bw, 24.0f)) {
                session.reset_demo();
            }
        }
    }

    // ================= FOOTER =============================================
    {
        const char* mode = session.symmetry() == Symmetry::MirrorVertical ? "MIRROR ON" : "MIRROR OFF";
        const char* grid = state.grid_visible ? "GRID ON" : "GRID OFF";
        std::snprintf(buffer, sizeof(buffer), "WHEEL ZOOM    LMB PLACE    X REMOVE    1/2/3 PRESETS    %s    %s",
                      mode, grid);
        ui.text(vw * 0.5f, vh - 15.0f, 10.0f, buffer, c_faint, aegis::ui::TextAlign::Center);
    }

    // Toast line.
    if (wall_clock_s - session.toast_time() < 3.0 && !session.toast().empty()) {
        ui.text(vw * 0.5f, 66.0f, 13.0f, session.toast().c_str(), aegis::ui::rgb(0.98f, 0.87f, 0.45f, 1.0f),
                aegis::ui::TextAlign::Center, aegis::ui::FontWeight::SemiBold);
    }

    aegis::render::submit_ui(renderer, camera, ui);

    // Palette part icons — drawn after the UI so they sit ON the buttons.
    for (core::u32 vi = 0; vi < visible_count; ++vi) {
        const int i = visible[vi];
        const aegis::parts::PartTemplate* tmpl = aegis::parts::find_template(static_cast<parts::TemplateId>(i));
        if (tmpl == nullptr) {
            continue;
        }
        const std::size_t column = vi % 2, row = vi / 2;
        const float bx = cell_x0 + static_cast<float>(column) * (cell_w + cell_gap);
        const float by = cell_y0 + static_cast<float>(row) * (cell_h + cell_gap);
        if (by + cell_h > palette.y + palette.h - 6.0f) {
            break;
        }
        const float cx = bx + cell_w * 0.5f;
        const float cy = by + 15.0f;
        const auto world = camera.screen_to_world(cx, cy);
        const core::f32 wpp = camera.world_per_pixel();
        core::f32 iw = static_cast<core::f32>(tmpl->h) > 0.0f
                           ? 26.0f * (static_cast<core::f32>(tmpl->w) / static_cast<core::f32>(tmpl->h)) * wpp
                           : 26.0f * wpp;
        core::f32 ih = 26.0f * wpp;
        const core::f32 max_w = (cell_w - 24.0f) * wpp;
        if (iw > max_w) {
            iw = max_w;
            ih = iw * (static_cast<core::f32>(tmpl->h) / static_cast<core::f32>(tmpl->w));
        }
        part_renderer.draw_single(renderer, *tmpl, 3000 + static_cast<core::u32>(i),
                                  static_cast<core::f32>(world[0]), static_cast<core::f32>(world[1]), iw, ih,
                                  0.0f, options, 0u);
    }
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
    state.map_ballistic_arc.clear();
    state.map_impact_pending = false;
    state.map_impact_in_s = -1.0;
    state.map_cache_flight_time = 0.0;
    state.map_t_ap_s = -1.0;
    state.map_t_pe_s = -1.0;

    const aegis::physics::FlightState& fs = flight.state();
    const core::f64 speed2 = fs.velocity.x * fs.velocity.x + fs.velocity.y * fs.velocity.y;
    state.map_elements = aegis::orbit::elements_from_state({fs.position.x, fs.position.y},
                                                           {fs.velocity.x, fs.velocity.y}, kEarthMu, fs.time);
    if (speed2 < 1.0) {
        return;  // parked on the pad — nothing to predict; the map shows the horizon only
    }
    const bool closed_orbit = state.map_elements.eccentricity < 1.0 &&
                              aegis::orbit::periapsis_radius(state.map_elements) >= kEarthRadius;
    if (closed_orbit) {
        state.map_points = aegis::orbit::sample_path(state.map_elements, 128);
        for (auto& point : state.map_points) {
            point[1] = static_cast<core::f32>(static_cast<core::f64>(point[1]) - kEarthRadius);  // rebase
        }
        state.map_apoapsis_alt = aegis::orbit::apoapsis_radius(state.map_elements) - kEarthRadius;
        state.map_periapsis_alt = aegis::orbit::periapsis_radius(state.map_elements) - kEarthRadius;
        state.map_period_s = aegis::orbit::orbital_period(state.map_elements);

        // Time to the next apoapsis / periapsis passage. periapsis_time is the
        // absolute time of the last (or next) periapsis; re-derive the mean
        // anomaly at `fs.time` and measure to M = pi (apo) and M = 2*pi (peri).
        const core::f64 two_pi = 2.0 * static_cast<core::f64>(kPi);
        const core::f64 pi = static_cast<core::f64>(kPi);
        const core::f64 n = aegis::orbit::mean_motion(state.map_elements);
        const core::f64 period = two_pi / n;
        core::f64 M = n * (fs.time - state.map_elements.periapsis_time);
        M = std::fmod(M, two_pi);
        if (M < 0.0) {
            M += two_pi;
        }
        core::f64 t_ap = (pi - M) / n;
        if (t_ap < 0.0) {
            t_ap += period;
        }
        state.map_t_ap_s = t_ap;
        state.map_t_pe_s = M == 0.0 ? 0.0 : (two_pi - M) / n;
        return;
    }

    // Not a closed orbit: either unbound, or bound-but-impacting. Integrate
    // the gravity-only arc to find the impact point and the time to it.
    const aegis::orbit::BallisticPrediction prediction =
        aegis::orbit::predict_ballistic({fs.position.x, fs.position.y}, {fs.velocity.x, fs.velocity.y},
                                        kEarthMu, kEarthRadius, 3600.0);
    state.map_ballistic_arc.reserve(prediction.arc.size());
    for (const auto& point : prediction.arc) {
        state.map_ballistic_arc.push_back({static_cast<core::f32>(point[0]),
                                           static_cast<core::f32>(point[1] - kEarthRadius)});
    }
    if (state.map_elements.eccentricity < 1.0) {
        // Suborbital ellipse — AP still meaningful; PE (below surface) is not.
        state.map_apoapsis_alt = aegis::orbit::apoapsis_radius(state.map_elements) - kEarthRadius;
    }
    if (prediction.impacts) {
        state.map_impact_pending = true;
        state.map_impact_in_s = prediction.impact_in_s;
        state.map_cache_flight_time = fs.time;
        state.map_impact_xy = {static_cast<core::f32>(prediction.impact_position[0]),
                            static_cast<core::f32>(prediction.impact_position[1] - kEarthRadius)};
    }
}

/// Draws the trajectory polyline in two passes: a wide, faint glow line
/// followed by a narrow bright core. `warm` styles ballistic (suborbital)
/// arcs orange; bound/orbital arcs draw cool blue. Segments dipping under
/// the surface are clipped out.
void draw_trajectory(BatchRenderer& renderer, const aegis::renderer2d::Camera2D& camera,
                     const std::vector<std::array<core::f32, 2>>& points, const bool warm) noexcept {
    if (points.size() < 2) {
        return;
    }
    const core::f32 wpp = camera.world_per_pixel();
    const core::f32 thickness = wpp * 2.2f;
    const core::f32 glow_thickness = wpp * 6.5f;
    const core::f32 glow_alpha = warm ? 0.16f : 0.13f;
    const core::f32 glow_r = warm ? 1.0f : 0.42f;
    const core::f32 glow_g = warm ? 0.72f : 0.62f;
    const core::f32 glow_b = warm ? 0.42f : 0.98f;
    const core::f32 core_r = warm ? 0.98f : 0.46f;
    const core::f32 core_g = warm ? 0.78f : 0.66f;
    const core::f32 core_b = warm ? 0.50f : 1.0f;
    auto above_surface = [&](const std::array<core::f32, 2>& p) {
        const core::f64 x = p[0], y = static_cast<core::f64>(p[1]) + kEarthRadius;
        return x * x + y * y >= kEarthRadius * kEarthRadius;
    };
    // Pass 1 — the glow bed.
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        if (!above_surface(points[i]) || !above_surface(points[i + 1])) {
            continue;
        }
        renderer.draw_line(points[i][0], points[i][1], points[i + 1][0], points[i + 1][1], glow_r, glow_g,
                           glow_b, glow_alpha, glow_thickness, 0.1f);
    }
    // Pass 2 — the bright core.
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        if (!above_surface(points[i]) || !above_surface(points[i + 1])) {
            continue;
        }
        renderer.draw_line(points[i][0], points[i][1], points[i + 1][0], points[i + 1][1], core_r, core_g,
                           core_b, 0.85f, thickness, 0.1f);
    }
}

/// Impact marker: a red-orange dot on the horizon line with a thin cross.
void draw_impact_marker(BatchRenderer& renderer, const aegis::renderer2d::Camera2D& camera,
                        const std::array<core::f32, 2>& xy) noexcept {
    const core::f32 wpp = camera.world_per_pixel();
    const core::f32 dot_size = 13.0f * wpp;
    aegis::renderer2d::SpriteInstance dot{};
    dot.x = xy[0];
    dot.y = xy[1];
    dot.width = dot_size;
    dot.height = dot_size;
    dot.r = 1.0f;
    dot.g = 0.38f;
    dot.b = 0.22f;
    dot.a = 1.0f;
    dot.softness = -1.0f;
    renderer.draw_sprite(dot);
    for (const core::f32 angle : {0.78539816339f, -0.78539816339f}) {
        aegis::renderer2d::SpriteInstance bar{};
        bar.x = xy[0];
        bar.y = xy[1];
        bar.width = 24.0f * wpp;
        bar.height = 2.4f * wpp;
        bar.rotation = angle;
        bar.r = 1.0f;
        bar.g = 0.55f;
        bar.b = 0.30f;
        bar.a = 0.95f;
        bar.softness = -1.0f;
        renderer.draw_sprite(bar);
    }
}

/// Deep-space horizon for the orbital map. The game world is a real-scale
/// Earth (R = 6.371e6 m), so the surface is a straight line across the view
/// and the curvature is kilometres away — draw the line + atmosphere glow
/// directly instead of planet-scale discs (their sub-surface limb never
/// reaches the view, which left the map blank).
void draw_map_horizon(BatchRenderer& renderer, const aegis::renderer2d::Camera2D& camera) noexcept {
    const core::f32 vw = camera.visible_world_width();
    const core::f32 vh = camera.visible_world_height();
    const core::f32 wpp = camera.world_per_pixel();
    const core::f32 x = camera.x();
    auto rect = [&](const core::f32 cy, const core::f32 h, const core::f32 r, const core::f32 g,
                    const core::f32 b, const core::f32 a) {
        aegis::renderer2d::SpriteInstance s{};
        s.x = x;
        s.y = cy;
        s.width = vw + 4.0f * wpp;
        s.height = h + 0.75f * wpp;  // overlap so f32 rounding never opens a seam
        s.r = r;
        s.g = g;
        s.b = b;
        s.a = a;
        s.rotation = 0.0f;
        s.softness = 0.0f;
        renderer.draw_sprite(s);
    };
    // Opaque ground below y == 0 (reaches past the bottom of any allowed view).
    rect(-vh * 0.5f, vh, 0.075f, 0.11f, 0.09f, 1.0f);
    // Atmosphere glow: hard bands above the surface, fading out upward.
    constexpr core::f32 kGlowMeters = 9000.0f;
    constexpr int kBands = 8;
    const core::f32 band = kGlowMeters / static_cast<core::f32>(kBands);
    for (int i = 0; i < kBands; ++i) {
        const core::f32 t = static_cast<core::f32>(i) / static_cast<core::f32>(kBands);
        const core::f32 cy = (static_cast<core::f32>(i) + 0.5f) * band;
        const core::f32 a = 0.34f * std::pow(1.0f - t, 1.6f);
        rect(cy, band, 0.52f, 0.72f, 0.94f, a);
    }
}

/// Screen-space AP/PE tag next to a marker.
void draw_apsis_label(aegis::ui::UiContext& ui, const aegis::renderer2d::Camera2D& camera,
                      const core::f64 angle, const core::f64 radius_abs, const char* tag) noexcept {
    const core::f32 mx = static_cast<core::f32>(radius_abs * std::cos(angle));
    const core::f32 my = static_cast<core::f32>(radius_abs * std::sin(angle) - kEarthRadius);
    const auto [sx, sy] = camera.world_to_screen(mx, my);
    ui.text(sx + 12.0f, sy - 6.0f, 10.0f, tag, aegis::ui::rgb(0.62f, 0.68f, 0.78f));
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
        // Frame the visible arc: the true apoapsis when bound, else a local-hop
        // floor (the unbound Keplerian elements are a radial launch artifact).
        const core::f64 altitude_now = fs.position.y - kEarthRadius;
        const core::f64 apo = std::max(state.map_apoapsis_alt, altitude_now + 800.0);
        const core::f64 fit_height = apo * 1.45 + 1500.0;
        const core::f32 map_zoom = static_cast<core::f32>(static_cast<core::f64>(vh) / fit_height);
        if (!state.map_seeded) {
            // Snap on the first map frame so the drawn pose and the capture
            // projection agree exactly (no one-frame glide offset).
            state.map_rig.snap(0.0, rocket_y * 0.30, map_zoom);
            state.map_seeded = true;
        } else {
            state.map_rig.set_target(0.0, rocket_y * 0.30);
            state.map_rig.set_zoom(map_zoom);
            state.map_rig.update(1.0 / 60.0);
        }
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
        aegis::render::draw_sky(renderer, camera, /*altitude_fraction=*/1.0f, /*star_seed=*/0xA0B1E5u);
        draw_map_horizon(renderer, camera);
        if (!state.map_points.empty()) {
            draw_trajectory(renderer, camera, state.map_points, /*warm=*/false);
        } else if (!state.map_ballistic_arc.empty()) {
            draw_trajectory(renderer, camera, state.map_ballistic_arc, /*warm=*/true);
        }
        if (state.map_impact_pending) {
            draw_impact_marker(renderer, camera, state.map_impact_xy);
            const auto [ix, iy] = camera.world_to_screen(state.map_impact_xy[0], state.map_impact_xy[1]);
            ui.text(ix + 12.0f, iy + 2.0f, 9.0f, "IMPACT", aegis::ui::rgb(1.0f, 0.55f, 0.35f),
                aegis::ui::TextAlign::Left, aegis::ui::FontWeight::SemiBold);
        }
        // Apoapsis / periapsis markers.
        const core::f64 omega = state.map_elements.periapsis_angle;
        const core::f64 peri_alt_check = session.periapsis_m();
        const bool suborbital = peri_alt_check < 0.0 || state.map_elements.eccentricity >= 1.0;
        if (state.map_elements.eccentricity < 1.0) {
            const core::f64 a = state.map_elements.semi_major_axis;
            const core::f64 e = state.map_elements.eccentricity;
            draw_apsis_marker(renderer, camera, omega + 3.14159265358979323846, a * (1.0 + e), true);
            if (a * (1.0 - e) > kEarthRadius) {
                draw_apsis_marker(renderer, camera, omega, a * (1.0 - e), false);
            }
        }
        // Prograde tick: short velocity-vector sprite from the vessel (world space).
        {
            const core::f64 vlen = std::hypot(fs.velocity.x, fs.velocity.y);
            if (vlen > 1.0) {
                const core::f64 nx = fs.velocity.x / vlen, ny = fs.velocity.y / vlen;
                const core::f32 wpp = camera.world_per_pixel();
                const core::f32 len_wu = 34.0f * wpp;
                const core::f32 offset_wu = 15.0f * wpp;
                aegis::renderer2d::SpriteInstance tick{};
                tick.x = static_cast<core::f32>(rocket_x) +
                         static_cast<core::f32>(nx) * (offset_wu + len_wu * 0.5f);
                tick.y = static_cast<core::f32>(rocket_y) +
                         static_cast<core::f32>(ny) * (offset_wu + len_wu * 0.5f);
                tick.width = len_wu;
                tick.height = 2.4f * wpp;
                tick.rotation = static_cast<core::f32>(std::atan2(ny, nx));
                tick.r = 0.98f;
                tick.g = 0.90f;
                tick.b = 0.55f;
                tick.a = 0.9f;
                tick.softness = -1.0f;
                renderer.draw_sprite(tick);
                const auto [sx, sy] = camera.world_to_screen(tick.x, tick.y);
                ui.text(sx + 6.0f, sy - 5.0f, 10.0f, "PRO", aegis::ui::rgb(0.98f, 0.90f, 0.55f, 0.85f));
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

        if (state.map_elements.eccentricity < 1.0) {
            const core::f64 a = state.map_elements.semi_major_axis;
            const core::f64 e = state.map_elements.eccentricity;
            draw_apsis_label(ui, camera, omega + 3.14159265358979323846, a * (1.0 + e), "AP");
            if (a * (1.0 - e) > kEarthRadius) {
                draw_apsis_label(ui, camera, omega, a * (1.0 - e), "PE");
            }
        }

        // For the degenerate radial case the Keplerian elements are numerically
        // unbound; show a local ballistic estimate instead of a bare -1.0.
        const bool unbound = state.map_elements.eccentricity >= 1.0;
        const core::f64 r_len = std::hypot(fs.position.x, fs.position.y);
        const core::f64 vr = (fs.velocity.x * fs.position.x + fs.velocity.y * fs.position.y) / r_len;
        const core::f64 h_ang = std::fabs(fs.position.x * fs.velocity.y - fs.position.y * fs.velocity.x);
        const core::f64 g_local = kEarthMu / (r_len * r_len);
        const core::f64 ballistic_apo =
            altitude + (vr > 0.0 ? (vr * vr) / (2.0 * g_local) : 0.0);
        const bool escaping = unbound && h_ang >= 1.0e3;
        const bool parked = session.speed_m_s() < 1.0 && altitude < 1.0;

        char buffer[160];
        ui.panel_frame(16.0f, 12.0f, 372.0f, 216.0f, kPanelFill, kPanelBorder, kAccentDim);
        ui.text(30.0f, 20.0f, 19.0f, "ORBITAL MAP", kTextHi, aegis::ui::TextAlign::Left,
                aegis::ui::FontWeight::SemiBold);
        ui.text(30.0f, 44.0f, 9.5f, "KEPLERIAN TRAJECTORY  ·  M TO RETURN", kTextFaint);
        ui.divider(30.0f, 60.0f, 344.0f, kPanelBorder);
        std::snprintf(buffer, sizeof(buffer), "APOAPSIS   %9.1f M", unbound ? ballistic_apo : session.apoapsis_m());
        ui.text(30.0f, 68.0f, 12.0f, buffer, aegis::ui::rgb(0.85f, 0.82f, 0.98f), aegis::ui::TextAlign::Left,
                aegis::ui::FontWeight::SemiBold);
        std::snprintf(buffer, sizeof(buffer), "PERIAPSIS  %9.1f M", unbound ? 0.0 : session.periapsis_m());
        ui.text(30.0f, 88.0f, 12.0f, buffer,
                suborbital ? aegis::ui::rgb(0.98f, 0.58f, 0.42f) : aegis::ui::rgb(0.75f, 0.92f, 0.75f),
                aegis::ui::TextAlign::Left, aegis::ui::FontWeight::SemiBold);
        std::snprintf(buffer, sizeof(buffer), "ALTITUDE   %9.1f M", altitude);
        ui.text(30.0f, 108.0f, 12.0f, buffer, kTextHi, aegis::ui::TextAlign::Left,
                aegis::ui::FontWeight::SemiBold);
        std::snprintf(buffer, sizeof(buffer), "VELOCITY   %9.1f M/S", session.speed_m_s());
        ui.text(30.0f, 128.0f, 12.0f, buffer, kTextHi, aegis::ui::TextAlign::Left,
                aegis::ui::FontWeight::SemiBold);
        if (parked) {
            ui.text(30.0f, 152.0f, 11.0f, "PARKED ON THE PAD", kTextDim, aegis::ui::TextAlign::Left,
                    aegis::ui::FontWeight::SemiBold);
        } else if (!suborbital) {
            std::snprintf(buffer, sizeof(buffer), "PERIOD     %9.1f S", state.map_period_s);
            ui.text(30.0f, 152.0f, 12.0f, buffer, kTextDim, aegis::ui::TextAlign::Left,
                    aegis::ui::FontWeight::SemiBold);
            if (state.map_t_ap_s >= 0.0 && state.map_t_pe_s >= 0.0) {
                std::snprintf(buffer, sizeof(buffer), "T+AP %6.1f S   T+PE %7.1f S", state.map_t_ap_s,
                              state.map_t_pe_s);
                ui.text(30.0f, 172.0f, 9.5f, buffer, kTextFaint);
            }
        } else if (state.map_impact_pending) {
            const core::f64 remaining =
                std::max(0.0, state.map_impact_in_s - (fs.time - state.map_cache_flight_time));
            std::snprintf(buffer, sizeof(buffer), "IMPACT T- %7.1f S", remaining);
            ui.text(30.0f, 152.0f, 13.0f, buffer, aegis::ui::rgb(0.99f, 0.62f, 0.35f),
                    aegis::ui::TextAlign::Left, aegis::ui::FontWeight::SemiBold);
        } else if (escaping) {
            ui.text(30.0f, 152.0f, 11.0f, "UNBOUND TRAJECTORY", aegis::ui::rgb(0.95f, 0.6f, 0.4f),
                    aegis::ui::TextAlign::Left, aegis::ui::FontWeight::SemiBold);
        } else {
            ui.text(30.0f, 152.0f, 11.0f, "BALLISTIC ARC", aegis::ui::rgb(0.95f, 0.6f, 0.4f),
                    aegis::ui::TextAlign::Left, aegis::ui::FontWeight::SemiBold);
        }
        if (!parked && suborbital && state.map_impact_pending) {
            ui.text(30.0f, 196.0f, 9.5f, "SUBORBITAL — TRAJECTORY IMPACTS THE SURFACE", kTextFaint);
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
    ui.panel_frame(12.0f, 12.0f, 236.0f, 168.0f, kPanelFill, kPanelBorder, kAccentDim);
    std::snprintf(buffer, sizeof(buffer), "T+%6.1f S", now);
    ui.text(24.0f, 20.0f, 11.0f, buffer, kTextDim, aegis::ui::TextAlign::Left, aegis::ui::FontWeight::SemiBold);
    ui.text(236.0f - 12.0f, 20.0f, 8.5f, "FLIGHT", kTextFaint, aegis::ui::TextAlign::Right);
    std::snprintf(buffer, sizeof(buffer), "ALT  %8.2f KM", altitude * 0.001);
    ui.text(24.0f, 42.0f, 16.0f, buffer, kTextHi, aegis::ui::TextAlign::Left, aegis::ui::FontWeight::SemiBold);
    std::snprintf(buffer, sizeof(buffer), "VEL  %8.1f M/S", session.speed_m_s());
    ui.text(24.0f, 66.0f, 16.0f, buffer, kTextHi, aegis::ui::TextAlign::Left, aegis::ui::FontWeight::SemiBold);
    std::snprintf(buffer, sizeof(buffer), "APO  %8.2f KM", std::max(session.apoapsis_m(), 0.0) * 0.001);
    ui.text(24.0f, 90.0f, 13.0f, buffer, aegis::ui::rgb(0.78f, 0.80f, 0.98f), aegis::ui::TextAlign::Left,
            aegis::ui::FontWeight::SemiBold);
    const core::f64 peri_hud = session.periapsis_m();
    const aegis::orbit::Elements hud_elements = session.orbital_elements();
    if (hud_elements.eccentricity < 1.0 && peri_hud < 0.0 && altitude > 1.0 && !session.impacted()) {
        ui.text(24.0f, 110.0f, 9.0f, "IMPACT PREDICTED", aegis::ui::rgb(0.98f, 0.62f, 0.38f),
                aegis::ui::TextAlign::Left, aegis::ui::FontWeight::SemiBold);
    } else if (!session.impacted()) {
        ui.text(24.0f, 110.0f, 9.0f, "ON ORBIT", aegis::ui::rgb(0.55f, 0.85f, 0.60f),
                aegis::ui::TextAlign::Left, aegis::ui::FontWeight::SemiBold);
    }
    ui.bar(24.0f, 132.0f, 212.0f, 13.0f, session.propellant_fraction(), aegis::ui::rgb(0.96f, 0.70f, 0.26f),
           aegis::ui::rgb(0.10f, 0.11f, 0.15f), aegis::ui::rgb(0.40f, 0.36f, 0.24f, 0.7f));
    ui.text(24.0f, 150.0f, 8.0f, "PROPELLANT", kTextFaint, aegis::ui::TextAlign::Left,
            aegis::ui::FontWeight::SemiBold);

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
    ui.text(vw - 82.0f, vh * 0.30f + vh * 0.34f + 8.0f, 11.0f, buffer, kTextHi, aegis::ui::TextAlign::Left,
            aegis::ui::FontWeight::SemiBold);

    // Stage button, bottom-right: shows the NEXT stage action.
    const auto& stages = flight->staging().stages();
    const core::u8 cursor = flight->staging().cursor();
    const char* stage_label = cursor < stages.size() ? "STAGE" : "OUT OF STAGES";
    const core::u64 stage_id = aegis::ui::UiContext::make_id("stage_button");
    if (ui.button(stage_id, vw - 236.0f, vh - 96.0f, 150.0f, 60.0f, stage_label,
                  cursor < stages.size() ? kAccentGreen : kNeutral)) {
        session.activate_stage();
    }
    const core::u32 next_stage =
        static_cast<core::u32>(std::min<std::size_t>(static_cast<std::size_t>(cursor) + 1u, stages.size()));
    std::snprintf(buffer, sizeof(buffer), "STAGE %u/%zu", stages.empty() ? 0u : next_stage, stages.size());
    ui.text(vw - 161.0f, vh - 118.0f, 9.0f, buffer, kTextDim, aegis::ui::TextAlign::Center,
            aegis::ui::FontWeight::SemiBold);

    // Controls hint, bottom-left.
    ui.text(16.0f, vh - 44.0f, 9.0f, "A/D OR Q/E  PITCH  ·  W/S  THROTTLE", kTextFaint);
    ui.text(16.0f, vh - 26.0f, 9.0f, "SPACE  STAGE  ·  M  MAP  ·  ESC  ABORT TO VAB", kTextFaint);

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
    // Seed the flight rig here (not only inside draw_flight) so the projection
    // computed before the draw pass matches the pose the draw pass will use
    // on the first frame after launch, abort or map toggle.
    if (!session.map_overlay() && !state.flight_rig_seeded) {
        if (const physics::VehicleFlight* flight = session.flight()) {
            const core::f64 rocket_y = flight->state().position.y - kEarthRadius;
            state.flight_rig.snap(flight->state().position.x, rocket_y + 3.0, 40.0f);
            state.flight_rig_seeded = true;
        }
    }
    if (session.map_overlay() && !state.map_seeded) {
        if (const physics::VehicleFlight* flight = session.flight()) {
            update_map_cache(state, *flight, 0.0);
            const core::f64 rocket_y = flight->state().position.y - kEarthRadius;
            const core::f64 apo = std::max(state.map_apoapsis_alt, rocket_y + 800.0);
            const core::f64 fit_height = apo * 1.45 + 1500.0;
            const core::f32 map_zoom = static_cast<core::f32>(
                static_cast<core::f64>(state.map_rig.camera().viewport_height()) / fit_height);
            state.map_rig.snap(0.0, rocket_y * 0.30, map_zoom);
            state.map_seeded = true;
        }
    }
    return session.map_overlay() ? state.map_rig.camera() : state.flight_rig.camera();
}

void scene_set_viewport(SceneState& state, const core::f32 width_px, const core::f32 height_px) noexcept {
    state.vab_rig.camera().set_viewport(width_px, height_px);
    state.flight_rig.camera().set_viewport(width_px, height_px);
    state.map_rig.camera().set_viewport(width_px, height_px);
}

}  // namespace aegis::game
