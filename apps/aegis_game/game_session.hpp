// Aegis2D — game session: VAB ↔ flight FSM, staging, launch (Milestone 9).
//
// Pure CPU state machine — no Vulkan, no windowing, no rendering. The window
// app and the verification tool both drive this class; unit tests exercise it
// headlessly. Determinism: all iteration is in stored index order; the build
// screen is a pure function of (template selection, cursor cell, symmetry,
// edits). The flight screen wraps physics::VehicleFlight (fixed 1/240 s steps)
// and rebases nothing — callers render with y' = y - body.radius.
#pragma once

#include <aegis/core/types.hpp>
#include <aegis/core/result.hpp>

#include "game_presets.hpp"
#include <aegis/orbit/orbit.hpp>
#include <aegis/parts/staging.hpp>
#include <aegis/parts/vehicle_assembly.hpp>
#include <aegis/physics/model.hpp>
#include <aegis/physics/vehicle_flight.hpp>

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace aegis::game {

enum class GameScreen : core::u8 { Build, Flight };
enum class Symmetry : core::u8 { None, MirrorVertical };

inline constexpr core::f64 kPadElevation = 1.7;  ///< m — rocket sits this far above the surface at t = 0

/// One placeable connection node near the cursor (world units). Collected so
/// the VAB can render every live snap point, not only the chosen one.
struct CompatibleNodeMarker {
    core::f32 x = 0.0f;
    core::f32 y = 0.0f;
    bool chosen = false;  ///< this marker is the current snap target
};

/// Placement preview for the VAB ghost. Recomputed lazily whenever the
/// (cursor, selection, build version) triple changes.
struct HoverPreview {
    bool valid = false;
    core::f32 origin_x = 0.0f;   ///< ghost footprint centre (wu)
    core::f32 origin_y = 0.0f;
    core::f32 width = 1.0f;      ///< rotated footprint dimensions (wu)
    core::f32 height = 1.0f;
    core::u8 rot = 0;            ///< placement rotation incl. potential flip bit
    core::f32 node_x = 0.0f;     ///< snap-target marker (wu) — valid only when has_target
    core::f32 node_y = 0.0f;
    bool has_target = false;
    // Node selection for the actual placement (kept in sync with the preview).
    core::u32 parent_index = parts::kInvalidPart;
    parts::NodeId parent_node = 0;
    parts::NodeId child_node = 0;
    core::u8 child_rot = 0;
    // Every currently placeable node within the snap radius (cap 96 markers).
    core::u32 node_marker_count = 0;
    std::array<CompatibleNodeMarker, 96> node_markers{};
};

class GameSession {
public:
    GameSession();  ///< loads the demo rocket (VAB auto-load) + auto staging

    // ------------------------------------------------------------ shared

    [[nodiscard]] GameScreen screen() const noexcept { return screen_; }
    void toggle_map_overlay() noexcept { map_overlay_ = screen_ == GameScreen::Flight && !map_overlay_; }
    [[nodiscard]] bool map_overlay() const noexcept { return map_overlay_; }
    [[nodiscard]] const std::string& toast() const noexcept { return toast_; }
    [[nodiscard]] core::f64 toast_time() const noexcept { return toast_time_; }  ///< flight clock at toast (s)
    [[nodiscard]] core::f64 flight_time() const noexcept { return flight_time_; }

    // ------------------------------------------------------------ build

    void select_template(parts::TemplateId id) noexcept;
    [[nodiscard]] parts::TemplateId selected_template() const noexcept { return selected_; }
    void cycle_symmetry() noexcept {
        symmetry_ = symmetry_ == Symmetry::None ? Symmetry::MirrorVertical : Symmetry::None;
    }
    [[nodiscard]] Symmetry symmetry() const noexcept { return symmetry_; }

    /// Load one of the kPresetCount built-in craft (build screen only; a no-op
    /// with a toast in flight). Rebuilds the assembly, re-assigns staging.
    void load_preset(core::u32 index) noexcept;
    [[nodiscard]] core::u32 preset_index() const noexcept { return preset_index_; }
    [[nodiscard]] const char* preset_name() const noexcept { return preset_info(preset_index_).name; }

    /// Cursor position in VAB world units (wu, y-up, origin at the pad).
    void set_cursor_world(core::f32 world_x, core::f32 world_y) noexcept;

    /// Attempt to place the selected template at the snap candidate under the
    /// cursor (plus its mirror twin when symmetry is on). Returns false (and
    /// sets a toast) when no valid snap fits.
    [[nodiscard]] bool place_selected() noexcept;
    /// Remove the part under the cursor (smallest footprint wins; pod allowed).
    /// Returns true when a part was removed.
    [[nodiscard]] bool remove_at_cursor() noexcept;
    void clear_assembly() noexcept;
    void reset_demo() noexcept;
    void auto_stage() noexcept;

    [[nodiscard]] const parts::VehicleAssembly& build_assembly() const noexcept { return build_; }
    [[nodiscard]] const parts::StagingList& staging() const noexcept { return staging_; }
    [[nodiscard]] core::u64 build_version() const noexcept { return build_version_; }
    [[nodiscard]] const HoverPreview& hover_preview() noexcept;  ///< lazily recomputed
    /// Where the last successful placement actually landed (snap node, or part
    /// centre for a pad-root drop) — for the VAB snap-flash effect. Set by
    /// place_selected(); unaffected by the recomputed ghost.
    [[nodiscard]] std::array<core::f32, 2> last_placement() const noexcept {
        return {last_placement_x_, last_placement_y_};
    }
    /// World x of the mirror symmetry axis — the command pod's centre (the
    /// dashed orange VAB axis). Falls back to the deck centreline (x = 0.5).
    [[nodiscard]] core::f32 symmetry_axis_wu() const noexcept;

    [[nodiscard]] core::f64 build_mass_kg() const noexcept;
    [[nodiscard]] core::f64 delta_v_estimate() const noexcept;  ///< gravity-assist-free ideal Δv (m/s)
    /// Thrust-to-weight at LIFTOFF: stage-0 engines (falling back to every
    /// engine when staging is empty) over total mass under standard gravity.
    [[nodiscard]] core::f64 twr_estimate() const noexcept;
    [[nodiscard]] core::u32 part_count() const noexcept;
    /// First blocking issue, or empty string when the rocket can fly.
    [[nodiscard]] std::string launch_blocker() const noexcept;
    /// Switch to the flight screen. No-op (returns false) when blocked.
    [[nodiscard]] bool launch() noexcept;
    /// Discard the flight and return to the VAB (the pre-launch build is kept).
    void abort_to_vab() noexcept;

    // ------------------------------------------------------------ flight

    void set_throttle(core::f64 throttle) noexcept;
    void adjust_throttle(core::f64 delta) noexcept;
    [[nodiscard]] core::f64 throttle() const noexcept;
    void set_pitch_input(core::f64 input) noexcept;
    /// Fire the next pending stage. Toasts the outcome.
    void activate_stage() noexcept;
    /// Advance the simulation (fixed-step inside VehicleFlight).
    void update(core::f64 dt_seconds) noexcept;

    [[nodiscard]] const physics::VehicleFlight* flight() const noexcept;
    [[nodiscard]] core::f64 altitude_m() const noexcept;      ///< above the sphere (|p| - R); 0 off-pad only
    [[nodiscard]] core::f64 speed_m_s() const noexcept;
    /// Apoapsis altitude (m); -1 when the trajectory is unbound (e >= 1) or below the surface.
    [[nodiscard]] core::f64 apoapsis_m() const noexcept;
    /// Periapsis altitude (m); -1 when unbound. May be negative (impacts).
    [[nodiscard]] core::f64 periapsis_m() const noexcept;
    /// Current propellant mass fraction across all tanks (1 = full).
    [[nodiscard]] core::f64 propellant_fraction() const noexcept;
    [[nodiscard]] physics::ContactState contact() const noexcept;
    [[nodiscard]] bool impacted() const noexcept;
    /// Keplerian elements of the current state (for the map overlay).
    [[nodiscard]] aegis::orbit::Elements orbital_elements() const noexcept;

private:
    void bump_build_version() noexcept;
    void recompute_preview() noexcept;
    void set_toast(const char* message) noexcept;

    GameScreen screen_ = GameScreen::Build;
    bool map_overlay_ = false;

    parts::VehicleAssembly build_;
    parts::StagingList staging_;
    parts::TemplateId selected_ = 1;  ///< Long-2 Tank
    Symmetry symmetry_ = Symmetry::None;
    core::f32 cursor_x_ = 0.0f, cursor_y_ = 0.0f;
    core::f32 last_placement_x_ = 0.0f, last_placement_y_ = 0.0f;
    HoverPreview preview_;
    bool preview_dirty_ = true;
    core::u64 build_version_ = 0;

    core::u32 preset_index_ = 0;  ///< build craft slot (VAB presets, keys 1..3)
    std::optional<physics::VehicleFlight> flight_;
    std::string toast_;
    core::f64 toast_time_ = -1.0e9;
    core::f64 flight_time_ = 0.0;
};

}  // namespace aegis::game
