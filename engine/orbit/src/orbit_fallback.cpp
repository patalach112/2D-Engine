// Aegis2D — C++ orbital-mechanics kernels (fallback / A/B oracle).
//
// This file holds the original Milestone 4 implementation, unchanged
// numerically. It is always compiled:
//   - when no Fortran compiler is available (or AEGIS_ENABLE_FORTRAN=OFF),
//     aegis::orbit dispatches here;
//   - when the Fortran kernels are enabled, this file remains the numerical
//     reference used by the A/B equivalence unit tests.
#include <aegis/orbit/internal.hpp>

#include <cmath>

namespace aegis::orbit::detail {

using core::f64;

namespace {

constexpr f64 kTwoPi = 6.2831853071795864769;

f64 normalize_angle(f64 angle) noexcept {
    // Bring into [0, 2*pi). Handles huge times without precision pathology.
    angle = std::fmod(angle, kTwoPi);
    if (angle < 0.0) {
        angle += kTwoPi;
    }
    // Guard fmod rounding landing exactly on 2*pi.
    if (angle >= kTwoPi) {
        angle -= kTwoPi;
    }
    return angle;
}

}  // namespace

f64 fallback_solve_kepler(const f64 mean_anomaly, const f64 eccentricity) noexcept {
    const f64 e = eccentricity;
    const f64 M = normalize_angle(mean_anomaly);

    // Cubic initial guess (Battin-style): accurate across the whole
    // eccentricity range without branchy iteration-count tuning.
    f64 E = M + e * std::sin(M) / (1.0 - std::sin(M + e) + std::sin(M) + 1e-9);
    if (!(E > -1e6) || !(E < 1e6) || std::isnan(E)) {
        E = M;  // fall back to M for pathological guesses
    }

    // Newton-Raphson: E_{n+1} = E_n - (E_n - e sin E_n - M) / (1 - e cos E_n)
    for (int iteration = 0; iteration < 32; ++iteration) {
        const f64 f = E - e * std::sin(E) - M;
        const f64 f_prime = 1.0 - e * std::cos(E);
        const f64 step = f / f_prime;
        E -= step;
        if (std::fabs(step) < 1e-14 * (1.0 + std::fabs(E))) {
            break;
        }
    }
    return E;
}

State fallback_state_at(const Elements& elements, const f64 t) noexcept {
    const f64 a = elements.semi_major_axis;
    const f64 e = elements.eccentricity;
    const f64 omega = elements.periapsis_angle;

    const f64 M = normalize_angle(
        std::sqrt(elements.gravitational_parameter / (a * a * a)) * (t - elements.periapsis_time));
    const f64 E = fallback_solve_kepler(M, e);

    const f64 cos_E = std::cos(E);
    const f64 sin_E = std::sin(E);

    // Position in the orbital frame (periapsis along +x'):
    //   x' = a (cos E - e),   y' = a sqrt(1 - e^2) sin E
    const f64 x_orbit = a * (cos_E - e);
    const f64 y_orbit = a * std::sqrt(1.0 - e * e) * sin_E;

    // Rotate by omega into the world frame.
    const f64 cos_w = std::cos(omega);
    const f64 sin_w = std::sin(omega);
    State state{};
    state.position = {cos_w * x_orbit - sin_w * y_orbit, sin_w * x_orbit + cos_w * y_orbit};

    // Velocity via E-derivative: dE/dt = n / (1 - e cos E).
    const f64 n = std::sqrt(elements.gravitational_parameter / (a * a * a));
    const f64 dE_dt = n / (1.0 - e * cos_E);
    const f64 vx_orbit = -a * sin_E * dE_dt;
    const f64 vy_orbit = a * std::sqrt(1.0 - e * e) * cos_E * dE_dt;
    state.velocity = {cos_w * vx_orbit - sin_w * vy_orbit, sin_w * vx_orbit + cos_w * vy_orbit};

    state.eccentric_anomaly = E;
    state.mean_anomaly = M;
    state.radius = std::hypot(state.position[0], state.position[1]);
    state.speed = std::hypot(state.velocity[0], state.velocity[1]);

    // True anomaly from the focus:  tan(theta/2) = sqrt((1+e)/(1-e)) tan(E/2)
    state.true_anomaly =
        normalize_angle(2.0 * std::atan2(std::sqrt(1.0 + e) * std::sin(E / 2.0),
                                         std::sqrt(1.0 - e) * std::cos(E / 2.0)));
    return state;
}

std::vector<std::array<core::f32, 2>> fallback_sample_path(const Elements& elements,
                                                           const core::u32 samples) {
    std::vector<std::array<core::f32, 2>> points;
    if (samples < 3) {
        return points;
    }
    points.reserve(samples);

    const f64 a = elements.semi_major_axis;
    const f64 e = elements.eccentricity;
    const f64 omega = elements.periapsis_angle;
    const f64 cos_w = std::cos(omega);
    const f64 sin_w = std::sin(omega);
    const f64 y_scale = std::sqrt(1.0 - e * e);

    for (core::u32 i = 0; i < samples; ++i) {
        const f64 E = kTwoPi * static_cast<f64>(i) / static_cast<f64>(samples);
        const f64 x_orbit = a * (std::cos(E) - e);
        const f64 y_orbit = a * y_scale * std::sin(E);
        const f64 wx = cos_w * x_orbit - sin_w * y_orbit;
        const f64 wy = sin_w * x_orbit + cos_w * y_orbit;
        points.push_back({static_cast<core::f32>(wx), static_cast<core::f32>(wy)});
    }
    return points;
}

}  // namespace aegis::orbit::detail
