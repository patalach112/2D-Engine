// Aegis2D — orbital mechanics (two-body Keplerian propagation), Vulkan-free.
//
// 2D plane in "world units" (same unit convention as the renderer: y up).
// All angles are radians; time is in seconds; `mu` is the gravitational
// parameter in world_units^3 / s^2.
//
// The solver is Newton-Raphson on Kepler's equation  M = E - e sin(E)  with a
// cubic initial guess; it converges to machine epsilon for 0 <= e < 1.
#pragma once

#include <aegis/export.h>

#include <aegis/core/types.hpp>

#include <array>
#include <vector>

namespace aegis::orbit {

struct AEGIS_API Elements {
    core::f64 semi_major_axis = 1.0;         ///< a — world units
    core::f64 eccentricity = 0.0;            ///< e — [0, 1)
    core::f64 periapsis_angle = 0.0;         ///< omega — angle of periapsis from world +X
    core::f64 gravitational_parameter = 1.0;  ///< mu — world_units^3 / s^2
    core::f64 periapsis_time = 0.0;          ///< tau — absolute time of periapsis passage
};

struct AEGIS_API State {
    std::array<core::f64, 2> position{};  ///< world units
    std::array<core::f64, 2> velocity{};  ///< world units / s
    core::f64 radius = 0.0;               ///< |position| from the focus (central body)
    core::f64 speed = 0.0;                ///< |velocity|
    core::f64 true_anomaly = 0.0;         ///< radians, measured from +periapsis direction
    core::f64 mean_anomaly = 0.0;         ///< radians (Kepler)
    core::f64 eccentric_anomaly = 0.0;    ///< radians
};

[[nodiscard]] AEGIS_API core::f64 orbital_period(const Elements& elements) noexcept;  ///< T = 2*pi*sqrt(a^3/mu)
[[nodiscard]] constexpr core::f64 periapsis_radius(const Elements& elements) noexcept {
    return elements.semi_major_axis * (1.0 - elements.eccentricity);
}
[[nodiscard]] constexpr core::f64 apoapsis_radius(const Elements& elements) noexcept {
    return elements.semi_major_axis * (1.0 + elements.eccentricity);
}
[[nodiscard]] AEGIS_API core::f64 mean_motion(const Elements& elements) noexcept;     ///< n = sqrt(mu/a^3)

/// Derive Keplerian elements from a state vector at absolute time `t`.
/// `periapsis_angle` is set to 0 for circular orbits (periapsis is undefined).
/// Unbound trajectories (energy >= 0) produce eccentricity >= 1 with a negative
/// semi-major axis; `state_at()` must NOT be called on those (precondition e < 1).
[[nodiscard]] AEGIS_API Elements elements_from_state(std::array<core::f64, 2> position,
                                          std::array<core::f64, 2> velocity,
                                          core::f64 gravitational_parameter, core::f64 t) noexcept;

/// Kepler's equation solver. M is normalized to [0, 2*pi).
/// Precondition: 0 <= eccentricity < 1.
[[nodiscard]] AEGIS_API core::f64 solve_kepler(core::f64 mean_anomaly, core::f64 eccentricity) noexcept;

/// Propagate the orbit to absolute time `t` (seconds).
[[nodiscard]] AEGIS_API State state_at(const Elements& elements, core::f64 t) noexcept;

/// Sample one full revolution as a polyline, evenly spaced in eccentric
/// anomaly (best visual spacing for eccentric orbits). `samples` points are
/// returned; the path is NOT closed — connect samples.back() <-> samples.front()
/// yourself if you want a closed loop (the demo does this).
[[nodiscard]] AEGIS_API std::vector<std::array<core::f32, 2>> sample_path(const Elements& elements,
                                                                core::u32 samples);

struct AEGIS_API BallisticPrediction {
    bool impacts = false;                        ///< true when the arc reaches the surface within max_t
    core::f64 impact_in_s = 0.0;                 ///< seconds from the input state to surface crossing
    std::array<core::f64, 2> impact_position{};  ///< world coordinates at the crossing point
    core::f64 impact_speed = 0.0;                ///< |velocity| at impact (world units / s)
    std::vector<std::array<core::f64, 2>> arc;   ///< trajectory polyline, world coordinates
};

/// Integrates the gravity-only (no thrust, no atmosphere) equations of motion
/// — a = -mu * r_vec / |r_vec|^3 — with a FIXED-STEP RK4 scheme, so identical
/// inputs always produce identical outputs. When the arc crosses `radius`, the
/// crossing time is refined by bracketed bisection of the last step (each
/// trial integrates a partial step with the same RK4 rule).
///
/// Returns `impacts == false` (with the outbound arc filled in, clipped at
/// `max_t`) for escape trajectories and for hops too flat to land in time.
/// Resolution is deterministic: short hops (constant-g estimate <= 300 s) get
/// ~300 steps to the crossing; anything else gets max_t / 1200. The loop is
/// hard-capped at 4096 iterations, so a borderline no-impact state yields a
/// truncated arc instead of running forever. A non-finite or non-positive mu,
/// or max_t <= 0, yields a default-constructed (non-impacting) result.
[[nodiscard]] AEGIS_API BallisticPrediction predict_ballistic(std::array<core::f64, 2> position,
                                                    std::array<core::f64, 2> velocity,
                                                    core::f64 gravitational_parameter,
                                                    core::f64 radius,
                                                    core::f64 max_t);

}  // namespace aegis::orbit
