// Aegis2D — pure physics model functions (Milestone 8).
//
// Deterministic, allocation-free kernels shared by the integrator and the unit
// tests. Exposed as a small public surface so tests can assert the analytic
// forms directly instead of only through end-to-end trajectories.
#pragma once

#include <aegis/physics/physics_types.hpp>

namespace aegis::physics::model {

/// Inverse-square point-mass gravity. Zero when mu == 0 or r == 0.
[[nodiscard]] Vec2d gravity_acceleration(const CelestialBody& body, const Vec2d& position) noexcept;

/// Exponential atmosphere: rho0 * exp(-altitude / H), exactly 0 at/above top.
/// altitude is measured above body.radius.
[[nodiscard]] f64 atmosphere_density(const CelestialBody& body, f64 altitude) noexcept;

/// Aerodynamic drag: opposes velocity, quadratic in speed. Zero when v == 0.
[[nodiscard]] Vec2d drag_force(const CelestialBody& body, const f64 rho, const Vec2d& velocity,
                               const AeroConfig& aero) noexcept;

/// Wrap an angle to (-pi, pi].
[[nodiscard]] f64 wrap_pi(f64 angle) noexcept;

/// The engine thrust axis in the part's LOCAL grid frame: the rotated outward
/// normal of the engine template's Top node (pointing away from the engine body
/// toward its mounting structure; rockets accelerate along it).
[[nodiscard]] Vec2d engine_thrust_local_axis(const parts::PartTemplate& engine_template,
                                             u8 part_rotation) noexcept;

}  // namespace aegis::physics::model
