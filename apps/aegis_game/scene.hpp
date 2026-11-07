// Aegis2D — scene rendering for aegis_game (VAB, flight, map overlay).
//
// Shared by the windowed app and the headless verification tool so both draw
// identical frames. The simulation itself lives in GameSession; this layer
// only turns session state into renderer2d calls. Owns the camera rigs,
// the map-cache (trajectory polyline, refreshed at 2 Hz) and the UiContext.
#pragma once

#include <aegis/core/types.hpp>
#include <aegis/orbit/orbit.hpp>
#include <aegis/render/camera_rig.hpp>
#include <aegis/ui/ui_context.hpp>
#include <aegis/ui/ui_input.hpp>
#include <aegis/ui/font_atlas.hpp>
#include <aegis/renderer2d/batch_renderer.hpp>

#include "game_session.hpp"

#include <array>
#include <vector>

namespace aegis::game {

/// Per-window scene state (rigs + caches + UI context). Call reset() whenever
/// the flight stage changes so the camera re-seeds onto the pad/vehicle.
struct SceneState {
    aegis::render::CameraRig vab_rig;   ///< fixed VAB framing rig (wheel zoom)
    aegis::render::CameraRig flight_rig;
    aegis::render::CameraRig map_rig;
    aegis::ui::FontAtlas font;   ///< baked once at startup; uploaded by the app to the GL
    aegis::ui::UiContext ui;
    bool delete_mode = false;           ///< VAB: click deletes instead of placing
    bool grid_visible = true;           ///< VAB: G toggles the construction grid
    int palette_filter = -1;            ///< VAB: -1 = all parts, else PartCategory cast to int
    core::f32 flash_x = 0.0f;           ///< VAB: world pos of the last "part snapped" flash
    core::f32 flash_y = 0.0f;
    core::f64 flash_time = -10.0;       ///< VAB: wall-clock seconds of that snap (for the expanding ring)
    bool flight_rig_seeded = false;     ///< re-seed camera on launch / reset
    bool map_seeded = false;            ///< first map frame snaps the framing (no glide)

    // Keplerian trajectory cache for the map view (render-side only).
    aegis::orbit::Elements map_elements{};
    std::vector<std::array<core::f32, 2>> map_points;  ///< y-rebased for rendering
    core::f64 map_apoapsis_alt = -1.0;
    core::f64 map_periapsis_alt = -1.0;
    core::f64 map_period_s = 0.0;
    core::f64 map_cache_time = -1.0e9;

    // Ballistic prediction cache (M10.2) — filled when the trajectory is NOT a
    // closed orbit above the surface (suborbital hop or open escape trajectory).
    std::vector<std::array<core::f32, 2>> map_ballistic_arc;  ///< y-rebased gravity-only polyline
    std::array<core::f32, 2> map_impact_xy{};                 ///< y-rebased impact point (valid when map_impact_pending)
    bool map_impact_pending = false;     ///< an impact is pending (trajectory reaches the surface)
    core::f64 map_impact_in_s = -1.0;    ///< seconds from the cache build to impact
    core::f64 map_cache_flight_time = 0.0;  ///< fs.time at the cache build (countdown base)
    core::f64 map_t_ap_s = -1.0;         ///< time to apoapsis (closed orbits; else -1)
    core::f64 map_t_pe_s = -1.0;         ///< time to periapsis (closed orbits; else -1)
};

void scene_state_init(SceneState& state) noexcept;
void scene_state_reset_flight(SceneState& state) noexcept;

/// Draw the VAB (grid, parts, palette UI, LAUNCH button). `wall_clock_s`
/// animates the ghost/node markers (VAB has no simulation clock).
/// Resize every rig's camera + call before the first frame and on window resize.
void scene_set_viewport(SceneState& state, core::f32 width_px, core::f32 height_px) noexcept;

/// The camera that frames this frame (VAB rig, flight rig, or map rig).
[[nodiscard]] aegis::renderer2d::Camera2D& scene_active_camera(SceneState& state, const GameSession& session) noexcept;

void draw_vab(aegis::renderer2d::BatchRenderer& renderer, SceneState& state,
              const aegis::ui::UiInput& input, GameSession& session, core::f64 wall_clock_s) noexcept;

/// Draw the flight view (or the full-screen map when the overlay is on).
void draw_flight(aegis::renderer2d::BatchRenderer& renderer, SceneState& state,
                 const aegis::ui::UiInput& input, GameSession& session) noexcept;

}  // namespace aegis::game
