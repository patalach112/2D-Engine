// Aegis2D — orbital mechanics (two-body Keplerian propagation), Vulkan-free.
//
// 2D plane in "world units" (same unit convention as the renderer: y up).
// All angles are radians; time is in seconds; `mu` is the gravitational
// parameter in world_units^3 / s^2.
//
// The solver is Newton-Raphson on Kepler's equation  M = E - e sin(E)  with a
// cubic initial guess; it converges to machine epsilon for 0 <= e < 1.
#pragma once

#include <aegis/core/types.hpp>

#include <array>
#include <vector>

namespace aegis::orbit {

struct Elements {
    core::f64 semi_major_axis = 1.0;         ///< a — world units
    core::f64 eccentricity = 0.0;            ///< e — [0, 1)
    core::f64 periapsis_angle = 0.0;         ///< omega — angle of periapsis from world +X
    core::f64 gravitational_parameter = 1.0;  ///< mu — world_units^3 / s^2
    core::f64 periapsis_time = 0.0;          ///< tau — absolute time of periapsis passage
};

struct State {
    std::array<core::f64, 2> position{};  ///< world units
    std::array<core::f64, 2> velocity{};  ///< world units / s
    core::f64 radius = 0.0;               ///< |position| from the focus (central body)
    core::f64 speed = 0.0;                ///< |velocity|
    core::f64 true_anomaly = 0.0;         ///< radians, measured from +periapsis direction
    core::f64 mean_anomaly = 0.0;         ///< radians (Kepler)
    core::f64 eccentric_anomaly = 0.0;    ///< radians
};

[[nodiscard]] core::f64 orbital_period(const Elements& elements) noexcept;  ///< T = 2*pi*sqrt(a^3/mu)
[[nodiscard]] constexpr core::f64 periapsis_radius(const Elements& elements) noexcept {
    return elements.semi_major_axis * (1.0 - elements.eccentricity);
}
[[nodiscard]] constexpr core::f64 apoapsis_radius(const Elements& elements) noexcept {
    return elements.semi_major_axis * (1.0 + elements.eccentricity);
}
[[nodiscard]] core::f64 mean_motion(const Elements& elements) noexcept;     ///< n = sqrt(mu/a^3)

/// Kepler's equation solver. M is normalized to [0, 2*pi).
/// Precondition: 0 <= eccentricity < 1.
[[nodiscard]] core::f64 solve_kepler(core::f64 mean_anomaly, core::f64 eccentricity) noexcept;

/// Propagate the orbit to absolute time `t` (seconds).
[[nodiscard]] State state_at(const Elements& elements, core::f64 t) noexcept;

/// Sample one full revolution as a polyline, evenly spaced in eccentric
/// anomaly (best visual spacing for eccentric orbits). `samples` points are
/// returned; the path is NOT closed — connect samples.back() <-> samples.front()
/// yourself if you want a closed loop (the demo does this).
[[nodiscard]] std::vector<std::array<core::f32, 2>> sample_path(const Elements& elements,
                                                                core::u32 samples);

}  // namespace aegis::orbit
