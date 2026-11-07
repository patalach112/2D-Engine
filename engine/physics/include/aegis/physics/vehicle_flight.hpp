// Aegis2D — VehicleFlight: rigid-body flight dynamics for an aegis_parts
// vehicle (Milestone 8).
//
// Owns the assembly and staging list by move. The flight position is the world
// position of the vehicle CoM (see create() — positions are passed in absolute
// planet-centred metres; the assembly keeps its internal grid coordinates).
// Propellant is drained once per fixed tick BEFORE the RK4 step, so a substep
// never sees half-drained tanks; mass/CoM/Izz re-aggregate lazily in M7.
#pragma once

#include <aegis/core/result.hpp>
#include <aegis/parts/staging.hpp>
#include <aegis/parts/vehicle_assembly.hpp>
#include <aegis/physics/physics_types.hpp>

#include <vector>

namespace aegis::physics {

class VehicleFlight {
public:
    // Fixed integration step (s). advance() loops it; tests use tick() for
    // exact determinism.
    static constexpr f64 kFixedDt = 1.0 / 240.0;

    /// Validate the config, the assembly (must exist and pass validation) and
    /// take ownership. `position`/`velocity` are absolute; heading defaults to
    /// straight up.
    [[nodiscard]] static core::Result<VehicleFlight> create(
        parts::VehicleAssembly assembly, parts::StagingList staging, const VehicleConfig& config,
        Vec2d position, Vec2d velocity, f64 heading = 1.5707963267948966);

    VehicleFlight(VehicleFlight&&) noexcept = default;
    VehicleFlight& operator=(VehicleFlight&&) noexcept = default;
    VehicleFlight(const VehicleFlight&) = delete;
    VehicleFlight& operator=(const VehicleFlight&) = delete;

    // --- Commands -----------------------------------------------------------

    /// Global throttle for all ignited (non-flameout) engines, clamped to [0, 1].
    void set_throttle(f64 throttle) noexcept;
    /// Pitch/attitude input in [-1, 1]; positive == CCW torque.
    void set_pitch_input(f64 input) noexcept;

    /// Fire the next pending stage (decouplers -> fairings -> ignitions) and
    /// wire the results into flight: engines join the lit set, decouplers
    /// split the vehicle (pod-bearing component is adopted) with a
    /// momentum-conserving inverse-mass separation impulse, and detached
    /// pieces become ballistic free-flyers tracked in detached_stages().
    /// The world position of every point of the adopted vehicle is continuous.
    [[nodiscard]] core::Result<void> activate_next_stage();

    /// One fixed RK4 step of duration `dt` (use tick(kFixedDt) or advance()).
    void tick(f64 dt);
    /// Advance `frame_dt` in fixed kFixedDt steps (remainder carried).
    void advance(f64 frame_dt);

    /// Drain pending events (for audio/UI); clears the internal list.
    void take_events(std::vector<FlightEvent>& out) noexcept;

    // --- Views ---------------------------------------------------------------

    [[nodiscard]] const FlightState& state() const noexcept { return state_; }
    [[nodiscard]] ContactState contact() const noexcept { return contact_; }
    [[nodiscard]] const parts::VehicleAssembly& assembly() const noexcept { return assembly_; }
    [[nodiscard]] const parts::MassProperties& mass_properties() const noexcept {
        return assembly_.mass_properties();
    }
    [[nodiscard]] const parts::StagingList& staging() const noexcept { return staging_; }
    [[nodiscard]] f64 throttle() const noexcept { return throttle_; }
    [[nodiscard]] f64 pitch_input() const noexcept { return pitch_input_; }
    [[nodiscard]] const std::vector<parts::PartIndex>& ignited_engines() const noexcept {
        return ignited_;
    }
    [[nodiscard]] const std::vector<DetachedStage>& detached_stages() const noexcept {
        return detached_;
    }
    [[nodiscard]] bool finished() const noexcept { return contact_ == ContactState::Impacted; }

    // --- Integrator internals (exposed for testing, not for gameplay) --------

    struct Derivative {
        Vec2d velocity_pos;       ///< d(position)/dt
        Vec2d acceleration;       ///< d(velocity)/dt
        f64 angular_velocity;     ///< d(heading)/dt
        f64 angular_acceleration; ///< d(angular_velocity)/dt
    };

private:
    VehicleFlight() = default;

    [[nodiscard]] Derivative evaluate(const FlightState& s, const parts::MassProperties& mp) const noexcept;
    void apply_contact_constraint() noexcept;
    void drain_propellant(f64 dt) noexcept;
    void apply_separation_impulses(std::vector<parts::VehicleAssembly>& pieces) noexcept;

    parts::VehicleAssembly assembly_;
    parts::StagingList staging_;
    VehicleConfig config_{};
    FlightState state_{};
    ContactState contact_ = ContactState::Airborne;
    f64 throttle_ = 0.0;
    f64 pitch_input_ = 0.0;
    f64 accumulator_ = 0.0;
    std::vector<parts::PartIndex> ignited_;     // sorted, unique
    std::vector<parts::PartIndex> flameout_;    // subset of ignited_
    std::vector<Vec2d> separation_velocities_;  // scratch, per activate_next_stage() call
    std::vector<DetachedStage> detached_;
    std::vector<FlightEvent> events_;
};

}  // namespace aegis::physics
