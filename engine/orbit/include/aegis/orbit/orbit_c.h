// Aegis2D — C ABI for the Fortran orbital-mechanics kernels (Milestone 6).
//
// Pure C, no structs by value, no allocations, no hidden state. All angles
// are radians; time is seconds; `mu` is world_units^3 / s^2. `out_*` buffers
// are written by the callee; array layout follows C sequence order.
//
// This header is always safe to include. The symbols exist only when the
// engine was configured with a Fortran compiler (AEGIS_HAVE_FORTRAN=1 on the
// aegis_orbit target); otherwise the C++ fallback kernels are used.
#ifndef AEGIS_ORBIT_C_H
#define AEGIS_ORBIT_C_H

#ifdef __cplusplus
extern "C" {
#endif

/// Kepler's equation solver. M is normalized to [0, 2*pi) before solving.
/// Precondition: 0 <= eccentricity < 1.
double aegis_solve_kepler_f64(double mean_anomaly, double eccentricity);

/// Propagate the orbit to absolute time `t` (seconds).
/// `out_state` layout (8 doubles):
///   [0] x        [1] y        [2] vx       [3] vy
///   [4] radius   [5] true anomaly  [6] mean anomaly   [7] eccentric anomaly
void aegis_state_at_f64(double semi_major_axis, double eccentricity, double periapsis_angle,
                        double gravitational_parameter, double periapsis_time, double t,
                        double out_state[8]);

/// Fill `out_xy` with 2*samples floats (x0, y0, x1, y1, ...), sampling one
/// full revolution evenly in eccentric anomaly. Precondition: samples >= 3 (values <= 2^31).
/// (Array parameter written unsized — the element count is 2*samples by contract and array
/// parameters decay to pointers; a parameterized bound is not valid C++.)
void aegis_sample_path_f32(double semi_major_axis, double eccentricity, double periapsis_angle,
                           int samples, float out_xy[]);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AEGIS_ORBIT_C_H
